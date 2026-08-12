#!/usr/bin/env python3
"""UDS / OBD-II cevaplarini insan tarafindan okunabilir hale getirir.

tools/uds.sh tarafindan cagrilir, ama tek basina da kullanilabilir:

    ./tools/udsdecode.py dtc 5902FF0104A10108...
    ./tools/udsdecode.py vin 62F1904A54444B...
    ./tools/udsdecode.py raw 7F2231

Lisans: MIT
"""

from __future__ import annotations

import sys
from typing import Sequence

# --------------------------------------------------------------------- #
# Tablolar                                                              #
# --------------------------------------------------------------------- #

# ISO 14229-1 negatif cevap kodlari.
NRC = {
    0x10: "genelHata (generalReject)",
    0x11: "servis desteklenmiyor",
    0x12: "alt fonksiyon desteklenmiyor",
    0x13: "istek uzunlugu hatali",
    0x14: "cevap cok uzun",
    0x21: "ECU mesgul, tekrar deneyin",
    0x22: "kosullar uygun degil (motor calisiyor olabilir)",
    0x24: "istek sirasi hatali",
    0x31: "istek araligi disinda (bu DID/PID yok)",
    0x33: "guvenlik erisimi reddedildi",
    0x35: "gecersiz anahtar",
    0x36: "cok fazla deneme",
    0x37: "gerekli bekleme suresi dolmadi",
    0x72: "programlama hatasi",
    0x78: "istek alindi, cevap bekleniyor",
    0x7E: "bu oturumda alt fonksiyon desteklenmiyor",
    0x7F: "bu oturumda servis desteklenmiyor",
    0x81: "devir cok yuksek",
    0x82: "devir cok dusuk",
    0x83: "motor calisiyor",
    0x84: "motor calismiyor",
    0x87: "arac hizi cok yuksek",
}

SERVICE = {
    0x01: "OBD-II anlik veri",
    0x03: "OBD-II kayitli DTC",
    0x07: "OBD-II bekleyen DTC",
    0x09: "OBD-II arac bilgisi",
    0x10: "DiagnosticSessionControl",
    0x11: "ECUReset",
    0x14: "ClearDiagnosticInformation",
    0x19: "ReadDTCInformation",
    0x22: "ReadDataByIdentifier",
    0x27: "SecurityAccess",
    0x2E: "WriteDataByIdentifier",
    0x31: "RoutineControl",
    0x3E: "TesterPresent",
}

# Standart ISO 14229 veri tanimlayicilari.
DID = {
    0xF186: "aktif tanilama oturumu",
    0xF187: "uretici parca numarasi",
    0xF188: "uretici yazilim surumu",
    0xF189: "uretici yazilim surum numarasi",
    0xF18A: "sistem tedarikcisi kimligi",
    0xF18C: "ECU seri numarasi",
    0xF190: "VIN",
    0xF191: "uretici donanim numarasi",
    0xF194: "tedarikci yazilim surumu",
    0xF195: "tedarikci yazilim surum numarasi",
}

# DTC ilk iki biti -> harf, sonraki iki bit -> ilk rakam.
DTC_LETTER = {0: "P", 1: "C", 2: "B", 3: "U"}

# ISO 14229-1 DTC durum baytindaki bitler.
DTC_STATUS_BITS = [
    (0x01, "testFailed (su an ariza var)"),
    (0x02, "testFailedThisOperationCycle"),
    (0x04, "pendingDTC (bekleyen)"),
    (0x08, "confirmedDTC (onaylanmis)"),
    (0x10, "testNotCompletedSinceLastClear"),
    (0x20, "testFailedSinceLastClear"),
    (0x40, "testNotCompletedThisOperationCycle"),
    (0x80, "warningIndicatorRequested (MIL yaniyor)"),
]


# --------------------------------------------------------------------- #
# Yardimcilar                                                           #
# --------------------------------------------------------------------- #


def parse_hex(text: str) -> bytes:
    cleaned = "".join(c for c in text if c not in " \t\r\n:")
    if len(cleaned) % 2:
        raise ValueError(f"hex uzunlugu cift olmali: {text!r}")
    try:
        return bytes.fromhex(cleaned)
    except ValueError as exc:
        raise ValueError(f"gecersiz hex: {text!r}") from exc


def check_negative(data: bytes) -> bool:
    """Negatif cevabi yazdirir. Negatifse True doner."""
    if len(data) >= 3 and data[0] == 0x7F:
        service = SERVICE.get(data[1], f"0x{data[1]:02X}")
        reason = NRC.get(data[2], f"bilinmeyen kod 0x{data[2]:02X}")
        print(f"  NEGATIF CEVAP: {service} reddedildi")
        print(f"    sebep: {reason}")
        if data[2] == 0x31:
            print("    ipucu: bu tanimlayici bu ECU'da yok; ident komutunu deneyin")
        if data[2] == 0x33:
            print("    ipucu: uretici seviyesinde tanilama oturumu gerekiyor")
        return True
    return False


def printable(data: bytes) -> str:
    return "".join(chr(b) if 32 <= b < 127 else "." for b in data)


def decode_dtc_3byte(b0: int, b1: int, b2: int) -> str:
    """ISO 14229 formatinda 3 baytlik DTC -> 'P0301' benzeri kod."""
    letter = DTC_LETTER[(b0 >> 6) & 0x03]
    return f"{letter}{(b0 >> 4) & 0x03}{b0 & 0x0F:X}{b1:02X}{b2:02X}"[:6]


def decode_dtc_2byte(b0: int, b1: int) -> str:
    """OBD-II mode 03 formatinda 2 baytlik DTC."""
    letter = DTC_LETTER[(b0 >> 6) & 0x03]
    return f"{letter}{(b0 >> 4) & 0x03}{b0 & 0x0F:X}{b1:02X}"


def print_status(status: int) -> None:
    active = [name for bit, name in DTC_STATUS_BITS if status & bit]
    print(f"      durum 0x{status:02X}: " + (", ".join(active) if active else "-"))


# --------------------------------------------------------------------- #
# Komutlar                                                              #
# --------------------------------------------------------------------- #


def cmd_vin(data: bytes) -> int:
    if check_negative(data):
        return 1

    payload = b""
    if len(data) >= 3 and data[0] == 0x62:          # UDS 22 F190 cevabi
        payload = data[3:]
    elif len(data) >= 3 and data[0] == 0x49:        # OBD-II 09 02 cevabi
        payload = data[3:]
    else:
        print(f"  beklenmeyen cevap: {data.hex().upper()}")
        return 1

    vin = payload.decode("ascii", errors="replace").strip("\x00 ")
    print(f"  VIN: {vin}")
    if len(vin) == 17:
        # WMI: ilk 3 karakter uretici/ulke, 10. karakter model yili.
        print(f"    WMI (uretici) : {vin[0:3]}")
        print(f"    VDS (arac)    : {vin[3:9]}")
        print(f"    VIS (seri)    : {vin[9:]}")
    else:
        print(f"    uyari: 17 karakter bekleniyordu, {len(vin)} geldi")
    return 0


def cmd_dtc(data: bytes) -> int:
    if check_negative(data):
        return 1

    if len(data) >= 3 and data[0] == 0x59 and data[1] == 0x02:
        # UDS ReadDTCInformation, reportDTCByStatusMask
        body = data[3:]
        if not body:
            print("  ariza kodu yok.")
            return 0
        if len(body) % 4:
            print(f"  uyari: 4'un kati olmayan uzunluk ({len(body)} bayt)")
        print(f"  {len(body) // 4} ariza kodu:")
        for i in range(0, len(body) - 3, 4):
            code = decode_dtc_3byte(body[i], body[i + 1], body[i + 2])
            print(f"    {code}")
            print_status(body[i + 3])
        return 0

    if len(data) >= 1 and data[0] == 0x43:
        # OBD-II mode 03
        body = data[2:] if len(data) > 1 else b""
        codes = []
        for i in range(0, len(body) - 1, 2):
            if body[i] == 0 and body[i + 1] == 0:
                continue
            codes.append(decode_dtc_2byte(body[i], body[i + 1]))
        if not codes:
            print("  ariza kodu yok.")
        else:
            print(f"  {len(codes)} ariza kodu:")
            for code in codes:
                print(f"    {code}")
        return 0

    print(f"  beklenmeyen cevap: {data.hex().upper()}")
    return 1


def cmd_did(did_text: str, data: bytes) -> int:
    did = int(did_text, 16)
    name = DID.get(did, "uretici tanimli")

    if len(data) >= 3 and data[0] == 0x7F:
        return 0  # bu DID desteklenmiyor: ident taramasinda sessizce gec

    if len(data) < 3 or data[0] != 0x62:
        return 0

    payload = data[3:]
    text = printable(payload)
    print(f"  {did_text.upper()}  {name}")
    print(f"    hex   : {payload.hex().upper()}")
    if any(32 <= b < 127 for b in payload):
        print(f"    metin : {text}")
    return 0


def cmd_raw(data: bytes) -> int:
    print(f"  ham cevap ({len(data)} bayt): {data.hex().upper()}")
    if check_negative(data):
        return 1

    if data:
        service = data[0]
        if service >= 0x40:
            name = SERVICE.get(service - 0x40, f"0x{service - 0x40:02X}")
            print(f"  pozitif cevap: {name}")
        print(f"  metin: {printable(data)}")
    return 0


# --------------------------------------------------------------------- #


def main(argv: Sequence[str]) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2

    mode = argv[0]
    try:
        if mode == "did":
            if len(argv) < 3:
                print("kullanim: udsdecode.py did <DID> <HEX>", file=sys.stderr)
                return 2
            return cmd_did(argv[1], parse_hex(argv[2]))

        data = parse_hex(argv[1])
        if mode == "vin":
            return cmd_vin(data)
        if mode == "dtc":
            return cmd_dtc(data)
        if mode == "raw":
            return cmd_raw(data)
    except ValueError as exc:
        print(f"hata: {exc}", file=sys.stderr)
        return 2

    print(f"bilinmeyen mod: {mode}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
