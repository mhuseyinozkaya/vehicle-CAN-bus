#!/usr/bin/env python3
"""tools/candiff.py icin testler. Bagimlilik yok: python3 test/test_candiff.py"""

import io
import os
import sys
import unittest
from contextlib import redirect_stdout
from tempfile import TemporaryDirectory

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import candiff  # noqa: E402


LOG_FORMAT = """\
(1699999999.000000) slcan0 7E8#03410C1AF8000000
(1699999999.100000) slcan0 7E8#03410C1B00000000
(1699999999.200000) slcan0 18DAF110#112233
"""

SCREEN_FORMAT = """\
  slcan0  7E8   [8]  03 41 0C 1A F8 00 00 00
  slcan0  123   [2]  DE AD
"""


class TestParsing(unittest.TestCase):
    def test_log_format(self):
        frames = list(candiff.parse_stream(io.StringIO(LOG_FORMAT), strict=True))
        self.assertEqual(len(frames), 3)
        self.assertEqual(frames[0].can_id, 0x7E8)
        self.assertFalse(frames[0].ext)
        self.assertEqual(frames[0].data, bytes.fromhex("03410C1AF8000000"))
        self.assertAlmostEqual(frames[0].ts, 1699999999.0)

    def test_extended_id_detected_by_width(self):
        frames = list(candiff.parse_stream(io.StringIO(LOG_FORMAT), strict=True))
        ext = frames[2]
        self.assertTrue(ext.ext)
        self.assertEqual(ext.can_id, 0x18DAF110)
        self.assertEqual(ext.id_str, "18DAF110")

    def test_screen_format(self):
        frames = list(candiff.parse_stream(io.StringIO(SCREEN_FORMAT), strict=True))
        self.assertEqual(len(frames), 2)
        self.assertEqual(frames[1].can_id, 0x123)
        self.assertEqual(frames[1].data, b"\xde\xad")

    def test_zero_length_frame(self):
        frames = list(candiff.parse_stream(
            io.StringIO("(1.0) slcan0 123#\n"), strict=True))
        self.assertEqual(frames[0].data, b"")

    def test_blank_and_comment_lines_ignored(self):
        text = "\n# yorum\n(1.0) slcan0 123#AA\n\n"
        frames = list(candiff.parse_stream(io.StringIO(text), strict=True))
        self.assertEqual(len(frames), 1)

    def test_garbage_raises_in_strict_mode(self):
        with self.assertRaises(candiff.ParseError):
            list(candiff.parse_stream(io.StringIO("bu bir log degil\n"),
                                      strict=True))

    def test_garbage_skipped_by_default(self):
        text = "bu bir log degil\n(1.0) slcan0 123#AA\n"
        err = io.StringIO()
        stderr, sys.stderr = sys.stderr, err
        try:
            frames = list(candiff.parse_stream(io.StringIO(text)))
        finally:
            sys.stderr = stderr
        self.assertEqual(len(frames), 1)
        self.assertIn("uyari", err.getvalue())

    def test_dlc_mismatch_is_an_error(self):
        with self.assertRaises(candiff.ParseError):
            list(candiff.parse_stream(
                io.StringIO("  slcan0  123   [4]  DE AD\n"), strict=True))


class TestStats(unittest.TestCase):
    def stats_for(self, text):
        frames = list(candiff.parse_stream(io.StringIO(text), strict=True))
        return candiff.collect(frames)

    def test_counts_and_changing_bytes(self):
        stats = self.stats_for(LOG_FORMAT)
        key = 0x7E8
        st = stats[key]
        self.assertEqual(st.count, 2)
        # Bayt 3 ve 4 degisiyor (1A F8 -> 1B 00), digerleri sabit.
        self.assertEqual(st.changing_bytes(), [3, 4])
        self.assertIn(0, st.constant_bytes())

    def test_period(self):
        stats = self.stats_for(LOG_FORMAT)
        self.assertAlmostEqual(stats[0x7E8].period_ms, 100.0, places=3)

    def test_single_frame_has_no_period(self):
        stats = self.stats_for("(1.0) slcan0 123#AA\n")
        self.assertIsNone(stats[0x123].period_ms)

    def test_extended_and_standard_ids_do_not_collide(self):
        # 0x123 standart ve 0x00000123 extended ayri sayilmali.
        text = ("(1.0) slcan0 123#AA\n"
                "(1.1) slcan0 00000123#BB\n")
        stats = self.stats_for(text)
        self.assertEqual(len(stats), 2)

    def test_toggled_bit_mask(self):
        text = ("(1.0) slcan0 200#00\n"
                "(1.1) slcan0 200#10\n")
        stats = self.stats_for(text)
        self.assertEqual(stats[0x200].toggled[0], 0x10)
        self.assertEqual(candiff.fmt_bits(0x10), "...1....")


class TestDiffCommand(unittest.TestCase):
    """Asil kullanim senaryosu: baseline vs. eylem sirasindaki log."""

    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, text):
        path = os.path.join(self.tmp.name, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(text)
        return path

    def run_diff(self, baseline, candidate, extra=()):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = candiff.main(["diff", baseline, candidate, *extra])
        return rc, out.getvalue()

    def test_finds_the_byte_that_only_changes_in_the_candidate(self):
        # 0x300 bayt 1: baseline'da hep 00, adayda 00 -> 04 (bir dugme biti)
        baseline = self.write("a.log", "".join(
            f"({1.0 + i * 0.1:.6f}) slcan0 300#0000\n" for i in range(5)))
        candidate = self.write("b.log",
                               "(2.0) slcan0 300#0000\n"
                               "(2.1) slcan0 300#0004\n"
                               "(2.2) slcan0 300#0000\n")

        rc, text = self.run_diff(baseline, candidate)
        self.assertEqual(rc, 0)
        self.assertIn("300", text)
        # Degisen bit maskesi 0x04 -> '.....1..'
        self.assertIn(".....1..", text)

    def test_reports_ids_that_appear_only_in_the_candidate(self):
        baseline = self.write("a.log", "(1.0) slcan0 100#00\n")
        candidate = self.write("b.log",
                               "(2.0) slcan0 100#00\n"
                               "(2.1) slcan0 4B1#DEAD\n")
        rc, text = self.run_diff(baseline, candidate)
        self.assertEqual(rc, 0)
        self.assertIn("Sadece aday logda", text)
        self.assertIn("4B1", text)

    def test_bytes_noisy_in_both_logs_are_not_reported_as_signal(self):
        # Bir sayac her iki logda da degisiyor: sinyal adayi degil.
        baseline = self.write("a.log",
                              "(1.0) slcan0 500#01\n"
                              "(1.1) slcan0 500#02\n"
                              "(1.2) slcan0 500#03\n")
        candidate = self.write("b.log",
                               "(2.0) slcan0 500#04\n"
                               "(2.1) slcan0 500#05\n"
                               "(2.2) slcan0 500#06\n")
        rc, text = self.run_diff(baseline, candidate)
        self.assertEqual(rc, 0)
        section = text.split("Baseline'da sabit, adayda degisen baytlar")[1]
        self.assertIn("(sonuc yok)", section)

    def test_missing_file_returns_error_code(self):
        out = io.StringIO()
        err = io.StringIO()
        stderr, sys.stderr = sys.stderr, err
        try:
            with redirect_stdout(out):
                rc = candiff.main(["diff", "/yok/a.log", "/yok/b.log"])
        finally:
            sys.stderr = stderr
        self.assertEqual(rc, 2)


class TestWatchCommand(unittest.TestCase):
    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = os.path.join(self.tmp.name, "w.log")
        with open(self.path, "w", encoding="utf-8") as fh:
            fh.write("(1.0) slcan0 3B3#0000\n"
                     "(1.1) slcan0 3B3#0000\n"
                     "(1.2) slcan0 3B3#0100\n"
                     "(1.3) slcan0 200#FF\n")

    def test_watch_marks_changed_bytes(self):
        out = io.StringIO()
        with redirect_stdout(out):
            rc = candiff.main(["watch", self.path, "--id", "3B3"])
        self.assertEqual(rc, 0)
        text = out.getvalue()
        self.assertIn("[01]", text)   # degisen bayt koseli parantez icinde
        self.assertNotIn("200", text.split("\n")[0])

    def test_changes_only_filters_repeats(self):
        out = io.StringIO()
        with redirect_stdout(out):
            candiff.main(["watch", self.path, "--id", "3B3", "--changes-only"])
        body = [ln for ln in out.getvalue().splitlines() if ln.startswith(" ")]
        self.assertEqual(len(body), 2)  # ilk frame + degisen frame

    def test_unknown_id_returns_error(self):
        err = io.StringIO()
        stderr, sys.stderr = sys.stderr, err
        try:
            rc = candiff.main(["watch", self.path, "--id", "999"])
        finally:
            sys.stderr = stderr
        self.assertEqual(rc, 1)


class TestSummaryCommand(unittest.TestCase):
    def test_summary_runs(self):
        with TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "s.log")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(LOG_FORMAT)
            out = io.StringIO()
            with redirect_stdout(out):
                rc = candiff.main(["summary", path])
            self.assertEqual(rc, 0)
            text = out.getvalue()
            self.assertIn("Farkli ID", text)
            self.assertIn("7E8", text)
            self.assertIn("18DAF110", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
