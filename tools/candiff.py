#!/usr/bin/env python3
"""candiff - candump loglarini analiz eder ve karsilastirir.

Arac CAN bus'ini tersine muhendislik yaparken temel is akisi sudur:

    1. Hicbir sey yapmadan bir log al          (baseline)
    2. Tek bir eylemi yap - cam ac, kapiyi kilitle - ve ikinci logu al
    3. Ikisini karsilastir: hangi CAN ID'nin hangi bayti sadece
       ikinci logda degisti?

Bu arac o karsilastirmayi otomatiklestirir. Bayt ve bit seviyesinde
calisir, cunku CAN mesajlarinda bir dugmenin durumu genellikle tek bir
bittir.

Kullanim
--------
    # Tek bir logu ozetle
    ./tools/candiff.py summary kayit.log

    # Iki logu karsilastir - en ilginc ID'ler en ustte
    ./tools/candiff.py diff baseline.log cam-acik.log

    # Bir ID'nin zaman icinde nasil degistigini gor
    ./tools/candiff.py watch kayit.log --id 3B3

Girdi formatlari
----------------
Her ikisi de desteklenir:

    (1699999999.123456) slcan0 7E8#03410C1AF8000000     candump -l
      slcan0  7E8   [8]  03 41 0C 1A F8 00 00 00        candump (ekran)

Bagimlilik yoktur: sadece Python 3 standart kutuphanesi.

Lisans: MIT
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from typing import Iterable, Iterator, Sequence

# --------------------------------------------------------------------- #
# Ayristirma                                                            #
# --------------------------------------------------------------------- #

# (1699999999.123456) slcan0 7E8#03410C1AF8000000
# (1699999999.123456) slcan0 18DAF110#1122 33  -> bosluklara toleransli
_RE_LOG = re.compile(
    r"^\((?P<ts>\d+\.\d+)\)\s+(?P<iface>\S+)\s+"
    r"(?P<id>[0-9A-Fa-f]+)(?P<kind>#|##)(?P<data>[0-9A-Fa-f]*)"
)

#   slcan0  7E8   [8]  03 41 0C 1A F8 00 00 00
_RE_SCREEN = re.compile(
    r"^\s*(?P<iface>\S+)\s+(?P<id>[0-9A-Fa-f]+)\s+\[(?P<dlc>\d+)\]\s+"
    r"(?P<data>(?:[0-9A-Fa-f]{2}\s*)*)"
)


@dataclass(frozen=True)
class Frame:
    ts: float
    iface: str
    can_id: int
    ext: bool
    data: bytes

    @property
    def id_str(self) -> str:
        return f"{self.can_id:08X}" if self.ext else f"{self.can_id:03X}"


class ParseError(Exception):
    pass


def parse_line(line: str, lineno: int = 0) -> Frame | None:
    """Tek bir satiri Frame'e cevirir. Bos/yorum satirlarinda None doner."""
    line = line.rstrip("\n")
    if not line.strip() or line.lstrip().startswith("#"):
        return None

    m = _RE_LOG.match(line)
    if m:
        raw_id = m.group("id")
        try:
            data = bytes.fromhex(m.group("data"))
        except ValueError as exc:
            raise ParseError(f"satir {lineno}: gecersiz veri: {line!r}") from exc
        # candump extended ID'leri her zaman 8 haneli yazar.
        ext = len(raw_id) > 3
        return Frame(float(m.group("ts")), m.group("iface"), int(raw_id, 16),
                     ext, data)

    m = _RE_SCREEN.match(line)
    if m:
        raw_id = m.group("id")
        hexstr = m.group("data").replace(" ", "")
        try:
            data = bytes.fromhex(hexstr)
        except ValueError as exc:
            raise ParseError(f"satir {lineno}: gecersiz veri: {line!r}") from exc
        dlc = int(m.group("dlc"))
        if len(data) != dlc:
            raise ParseError(
                f"satir {lineno}: DLC {dlc} ile {len(data)} bayt uyusmuyor"
            )
        ext = len(raw_id) > 3
        # Ekran formatinda zaman damgasi yok; sira numarasi kullanilir.
        return Frame(float(lineno), m.group("iface"), int(raw_id, 16), ext, data)

    raise ParseError(f"satir {lineno}: taninmayan format: {line!r}")


def parse_stream(lines: Iterable[str], strict: bool = False) -> Iterator[Frame]:
    for lineno, line in enumerate(lines, start=1):
        try:
            frame = parse_line(line, lineno)
        except ParseError as exc:
            if strict:
                raise
            print(f"uyari: {exc}", file=sys.stderr)
            continue
        if frame is not None:
            yield frame


def load(path: str, strict: bool = False) -> list[Frame]:
    if path == "-":
        return list(parse_stream(sys.stdin, strict))
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return list(parse_stream(fh, strict))


# --------------------------------------------------------------------- #
# Istatistik                                                            #
# --------------------------------------------------------------------- #


@dataclass
class IdStats:
    """Tek bir CAN ID icin toplanan bilgiler."""

    can_id: int
    ext: bool
    count: int = 0
    dlcs: set[int] = field(default_factory=set)
    first_ts: float | None = None
    last_ts: float | None = None
    # Bayt basina gorulen tum degerler (ilk 8 bayt).
    values: list[set[int]] = field(default_factory=lambda: [set() for _ in range(8)])
    # Bayt basina "hic degisen bitler" maskesi.
    toggled: list[int] = field(default_factory=lambda: [0] * 8)
    _first_payload: bytes | None = None

    @property
    def id_str(self) -> str:
        return f"{self.can_id:08X}" if self.ext else f"{self.can_id:03X}"

    @property
    def period_ms(self) -> float | None:
        """Ortalama tekrar araligi. Tek frame varsa None."""
        if self.count < 2 or self.first_ts is None or self.last_ts is None:
            return None
        span = self.last_ts - self.first_ts
        if span <= 0:
            return None
        return (span / (self.count - 1)) * 1000.0

    def add(self, f: Frame) -> None:
        self.count += 1
        self.dlcs.add(len(f.data))
        if self.first_ts is None:
            self.first_ts = f.ts
            self._first_payload = f.data
        self.last_ts = f.ts

        base = self._first_payload or b""
        for i, byte in enumerate(f.data[:8]):
            self.values[i].add(byte)
            if i < len(base):
                self.toggled[i] |= base[i] ^ byte

    def changing_bytes(self) -> list[int]:
        return [i for i in range(8) if len(self.values[i]) > 1]

    def constant_bytes(self) -> list[int]:
        return [i for i in range(8) if len(self.values[i]) == 1]


def collect(frames: Sequence[Frame]) -> dict[int, IdStats]:
    stats: dict[int, IdStats] = {}
    for f in frames:
        key = f.can_id | (0x20000000 if f.ext else 0)
        st = stats.get(key)
        if st is None:
            st = IdStats(f.can_id, f.ext)
            stats[key] = st
        st.add(f)
    return stats


# --------------------------------------------------------------------- #
# Bicimleme                                                             #
# --------------------------------------------------------------------- #


def fmt_bits(mask: int) -> str:
    """0b00010000 -> '...1....' seklinde okunabilir bit maskesi."""
    return "".join("1" if mask & (1 << (7 - i)) else "." for i in range(8))


def fmt_period(ms: float | None) -> str:
    if ms is None:
        return "     -"
    if ms >= 1000:
        return f"{ms / 1000:5.1f}s"
    return f"{ms:5.0f}ms"


def print_table(rows: list[list[str]], headers: list[str]) -> None:
    if not rows:
        print("  (sonuc yok)")
        return
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))
    line = "  ".join(h.ljust(w) for h, w in zip(headers, widths))
    print(line)
    print("-" * len(line))
    for row in rows:
        print("  ".join(c.ljust(w) for c, w in zip(row, widths)))


# --------------------------------------------------------------------- #
# Komutlar                                                              #
# --------------------------------------------------------------------- #


def cmd_summary(args: argparse.Namespace) -> int:
    frames = load(args.log, args.strict)
    if not frames:
        print("log bos veya ayristirilamadi", file=sys.stderr)
        return 1

    stats = collect(frames)
    span = frames[-1].ts - frames[0].ts

    print(f"Dosya      : {args.log}")
    print(f"Frame      : {len(frames)}")
    print(f"Farkli ID  : {len(stats)}")
    if span > 0:
        print(f"Sure       : {span:.2f} s  ({len(frames) / span:.0f} frame/s)")
    print()

    rows = []
    for st in sorted(stats.values(), key=lambda s: -s.count):
        changing = st.changing_bytes()
        rows.append([
            st.id_str,
            "ext" if st.ext else "std",
            str(st.count),
            ",".join(str(d) for d in sorted(st.dlcs)),
            fmt_period(st.period_ms),
            ",".join(str(b) for b in changing) if changing else "-",
        ])
    print_table(rows, ["ID", "tip", "adet", "DLC", "periyot", "degisen baytlar"])
    return 0


def cmd_diff(args: argparse.Namespace) -> int:
    a_frames = load(args.baseline, args.strict)
    b_frames = load(args.candidate, args.strict)
    if not a_frames or not b_frames:
        print("loglardan biri bos veya ayristirilamadi", file=sys.stderr)
        return 1

    a = collect(a_frames)
    b = collect(b_frames)

    only_b = sorted(set(b) - set(a))
    only_a = sorted(set(a) - set(b))

    print(f"baseline : {args.baseline}  ({len(a_frames)} frame, {len(a)} ID)")
    print(f"aday     : {args.candidate}  ({len(b_frames)} frame, {len(b)} ID)")
    print()

    if only_b:
        print("== Sadece aday logda gorulen ID'ler ==")
        print("   (eylem sirasinda ortaya cikan mesajlar - en guclu ipucu)")
        rows = [[b[k].id_str, str(b[k].count), fmt_period(b[k].period_ms)]
                for k in only_b]
        print_table(rows, ["ID", "adet", "periyot"])
        print()

    if only_a and args.show_missing:
        print("== Sadece baseline logda gorulen ID'ler ==")
        rows = [[a[k].id_str, str(a[k].count)] for k in only_a]
        print_table(rows, ["ID", "adet"])
        print()

    # Asil is: iki logda da olan ID'lerde, baseline'da sabit olup adayda
    # degisen baytlar. Bunlar aranan sinyalin en olasi tasiyicilaridir.
    print("== Baseline'da sabit, adayda degisen baytlar ==")
    print("   (aradiginiz sinyal buyuk ihtimalle burada)")
    rows = []
    for key in sorted(set(a) & set(b)):
        sa, sb = a[key], b[key]
        for i in range(8):
            if len(sa.values[i]) != 1 or len(sb.values[i]) <= 1:
                continue
            base = next(iter(sa.values[i]))
            new = sorted(sb.values[i])
            mask = 0
            for v in new:
                mask |= base ^ v
            rows.append([
                sb.id_str,
                str(i),
                f"{base:02X}",
                " ".join(f"{v:02X}" for v in new[:6])
                + (" ..." if len(new) > 6 else ""),
                fmt_bits(mask),
                str(len(new)),
            ])
    # Az sayida farkli deger = daha muhtemel bir durum biti; once onlar.
    rows.sort(key=lambda r: int(r[5]))
    print_table(rows, ["ID", "bayt", "baseline", "adaydaki degerler",
                       "degisen bitler", "n"])
    print()

    if args.verbose:
        print("== Her iki logda da degisen baytlar (muhtemelen sayac/sensor) ==")
        rows = []
        for key in sorted(set(a) & set(b)):
            sa, sb = a[key], b[key]
            shared = [i for i in range(8)
                      if len(sa.values[i]) > 1 and len(sb.values[i]) > 1]
            if shared:
                rows.append([sb.id_str,
                             ",".join(str(i) for i in shared)])
        print_table(rows, ["ID", "baytlar"])
    return 0


def cmd_watch(args: argparse.Namespace) -> int:
    frames = load(args.log, args.strict)
    target = int(args.id, 16)
    selected = [f for f in frames if f.can_id == target]
    if not selected:
        print(f"{args.id} bu logda yok", file=sys.stderr)
        return 1

    print(f"ID {args.id.upper()} - {len(selected)} frame")
    print()

    prev: bytes | None = None
    t0 = selected[0].ts
    shown = 0
    for f in selected:
        if args.changes_only and prev is not None and f.data == prev:
            continue
        marks = []
        for i, byte in enumerate(f.data):
            if prev is not None and i < len(prev) and prev[i] != byte:
                marks.append(f"[{byte:02X}]")
            else:
                marks.append(f" {byte:02X} ")
        print(f"{f.ts - t0:8.3f}  " + "".join(marks))
        prev = f.data
        shown += 1
        if args.limit and shown >= args.limit:
            print(f"... ({args.limit} satir sinirina ulasildi)")
            break
    return 0


# --------------------------------------------------------------------- #
# Giris                                                                 #
# --------------------------------------------------------------------- #


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="candiff",
        description="candump loglarini ozetler ve karsilastirir",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "ornek:\n"
            "  candump -l slcan0                # baseline al, Ctrl-C\n"
            "  candump -l slcan0                # eylemi yaparken al\n"
            "  ./tools/candiff.py diff candump-1.log candump-2.log\n"
        ),
    )
    p.add_argument("--strict", action="store_true",
                   help="ayristirilamayan satirda uyarmak yerine hata ver")
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("summary", help="tek bir logu ozetle")
    s.add_argument("log", help="candump log dosyasi ('-' = stdin)")
    s.set_defaults(func=cmd_summary)

    d = sub.add_parser("diff", help="iki logu karsilastir")
    d.add_argument("baseline", help="eylem yapilmadan alinan log")
    d.add_argument("candidate", help="eylem yapilirken alinan log")
    d.add_argument("-v", "--verbose", action="store_true",
                   help="her iki logda da degisen baytlari da goster")
    d.add_argument("--show-missing", action="store_true",
                   help="sadece baseline'da olan ID'leri de goster")
    d.set_defaults(func=cmd_diff)

    w = sub.add_parser("watch", help="tek bir ID'nin degisimini izle")
    w.add_argument("log", help="candump log dosyasi ('-' = stdin)")
    w.add_argument("--id", required=True, help="CAN ID (hex, orn. 3B3)")
    w.add_argument("--changes-only", action="store_true",
                   help="sadece payload degistiginde satir yaz")
    w.add_argument("--limit", type=int, default=200,
                   help="azami satir sayisi (0 = sinirsiz)")
    w.set_defaults(func=cmd_watch)

    return p


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except ParseError as exc:
        print(f"hata: {exc}", file=sys.stderr)
        return 2
    except FileNotFoundError as exc:
        print(f"hata: dosya bulunamadi: {exc.filename}", file=sys.stderr)
        return 2
    except BrokenPipeError:
        return 0
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
