#!/usr/bin/env python3
"""tools/udsdecode.py icin testler. python3 test/test_udsdecode.py"""

import io
import os
import sys
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import udsdecode  # noqa: E402


def run(argv):
    out = io.StringIO()
    with redirect_stdout(out):
        rc = udsdecode.main(argv)
    return rc, out.getvalue()


class TestVin(unittest.TestCase):
    def test_uds_vin(self):
        vin = "WF0XXXGBWX8J12345"
        payload = "62F190" + vin.encode("ascii").hex()
        rc, text = run(["vin", payload])
        self.assertEqual(rc, 0)
        self.assertIn(vin, text)
        self.assertIn("WF0", text)      # WMI ayristirildi

    def test_obd_vin(self):
        vin = "1G1PC5SB4E7123456"
        payload = "490201" + vin.encode("ascii").hex()
        rc, text = run(["vin", payload])
        self.assertEqual(rc, 0)
        # OBD cevabinda 3. bayt sira numarasi; VIN'in tamami yakalanmali.
        self.assertIn("PC5SB4E7123456", text)

    def test_short_vin_warns(self):
        payload = "62F190" + b"KISA".hex()
        rc, text = run(["vin", payload])
        self.assertEqual(rc, 0)
        self.assertIn("uyari", text)


class TestNegativeResponse(unittest.TestCase):
    def test_request_out_of_range(self):
        rc, text = run(["raw", "7F2231"])
        self.assertEqual(rc, 1)
        self.assertIn("NEGATIF", text)
        self.assertIn("istek araligi disinda", text)
        self.assertIn("ident", text)          # yardimci ipucu

    def test_security_access_denied_hint(self):
        rc, text = run(["raw", "7F2733"])
        self.assertEqual(rc, 1)
        self.assertIn("guvenlik erisimi reddedildi", text)

    def test_unknown_nrc_is_reported_not_crashed(self):
        rc, text = run(["raw", "7F22AB"])
        self.assertEqual(rc, 1)
        self.assertIn("bilinmeyen kod 0xAB", text)

    def test_vin_negative(self):
        rc, text = run(["vin", "7F2231"])
        self.assertEqual(rc, 1)
        self.assertIn("NEGATIF", text)


class TestDtc(unittest.TestCase):
    def test_uds_dtc_decoding(self):
        # 59 02 <mask> then DTC(3) + status(1)
        # 0x0301 -> P0301 (silindir 1 tekleme), durum 0x09
        rc, text = run(["dtc", "5902FF" + "030100" + "09"])
        self.assertEqual(rc, 0)
        self.assertIn("P0301", text)
        self.assertIn("confirmedDTC", text)
        self.assertIn("testFailed", text)

    def test_uds_dtc_letters(self):
        # Ilk iki bit harfi belirler: 00=P 01=C 10=B 11=U
        cases = [("030100", "P"), ("430100", "C"), ("830100", "B"),
                 ("C30100", "U")]
        for dtc, letter in cases:
            rc, text = run(["dtc", "5902FF" + dtc + "08"])
            self.assertEqual(rc, 0)
            self.assertTrue(text.strip().splitlines()[1].strip().startswith(letter),
                            f"{dtc} icin {letter} bekleniyordu:\n{text}")

    def test_uds_no_dtc(self):
        rc, text = run(["dtc", "5902FF"])
        self.assertEqual(rc, 0)
        self.assertIn("ariza kodu yok", text)

    def test_mil_bit_is_reported(self):
        rc, text = run(["dtc", "5902FF" + "030100" + "88"])
        self.assertIn("MIL yaniyor", text)

    def test_obd_mode03(self):
        # 43 <adet> then 2-byte DTC'ler
        rc, text = run(["dtc", "4302" + "0301" + "0420"])
        self.assertEqual(rc, 0)
        self.assertIn("P0301", text)
        self.assertIn("P0420", text)

    def test_obd_mode03_padding_ignored(self):
        rc, text = run(["dtc", "4301" + "0301" + "0000"])
        self.assertEqual(rc, 0)
        self.assertIn("1 ariza kodu", text)


class TestDid(unittest.TestCase):
    def test_known_did_named(self):
        payload = "62F18C" + b"ECU-SN-0042".hex()
        rc, text = run(["did", "F18C", payload])
        self.assertEqual(rc, 0)
        self.assertIn("ECU seri numarasi", text)
        self.assertIn("ECU-SN-0042", text)

    def test_unsupported_did_is_silent(self):
        rc, text = run(["did", "F186", "7F2231"])
        self.assertEqual(rc, 0)
        self.assertEqual(text.strip(), "")

    def test_binary_did_shows_hex_only(self):
        rc, text = run(["did", "F191", "62F191" + "00010203"])
        self.assertIn("00010203", text)
        self.assertNotIn("metin", text)


class TestRaw(unittest.TestCase):
    def test_positive_response_named(self):
        rc, text = run(["raw", "5001"])   # 0x10 + 0x40 = DiagnosticSessionControl
        self.assertEqual(rc, 0)
        self.assertIn("DiagnosticSessionControl", text)

    def test_invalid_hex(self):
        err = io.StringIO()
        stderr, sys.stderr = sys.stderr, err
        try:
            rc = udsdecode.main(["raw", "ZZ"])
        finally:
            sys.stderr = stderr
        self.assertEqual(rc, 2)

    def test_odd_length_hex(self):
        err = io.StringIO()
        stderr, sys.stderr = sys.stderr, err
        try:
            rc = udsdecode.main(["raw", "ABC"])
        finally:
            sys.stderr = stderr
        self.assertEqual(rc, 2)
        self.assertIn("cift olmali", err.getvalue())

    def test_whitespace_and_colons_tolerated(self):
        rc, text = run(["raw", "50 01"])
        self.assertEqual(rc, 0)
        rc, text = run(["raw", "50:01"])
        self.assertEqual(rc, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
