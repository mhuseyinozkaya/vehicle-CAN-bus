#!/usr/bin/env bash
#
# uds.sh - ISO-TP (ISO 15765-2) uzerinden UDS / OBD-II sorgulari.
#
# Tek CAN frame'ine sigmayan istekler icin taşıma katmanı gerekir. Bu
# script Linux'un can-isotp cekirdek modulunu ve can-utils'in
# isotpsend/isotprecv araclarini kullanir; firmware tarafinda hicbir
# degisiklik gerektirmez.
#
# Kullanim:
#   ./tools/uds.sh [-i IFACE] [-t TX_ID] [-r RX_ID] [-w SANIYE] <komut>
#
# Komutlar:
#   vin              Arac kimlik numarasi (UDS 22 F190, OBD 09 02'ye duser)
#   dtc              Kayitli ariza kodlari (UDS 19 02, OBD 03'e duser)
#   ident            ECU kimlik bilgileri (parca no, yazilim surumu, ...)
#   session          Tanilama oturumu bilgisi (10 01 - varsayilan oturum)
#   raw <HEX>        Ham UDS istegi, orn: raw 22F190
#   scan             7E0-7E7 araligindaki cevap veren ECU'lari bul
#
# Secenekler:
#   -i IFACE   SocketCAN arayuzu            (varsayilan: slcan0)
#   -t TX_ID   Tester -> ECU CAN ID (hex)   (varsayilan: 7E0)
#   -r RX_ID   ECU -> tester CAN ID (hex)   (varsayilan: 7E8)
#   -w SANIYE  Cevap bekleme suresi         (varsayilan: 2)
#   -f         Yazma/kontrol servisleri icin guvenlik kilidini ac
#
# GUVENLIK: Bu script bus'a YAZAR. Kontak acik, motor kapali, arac
# hareketsizken kullanin. Yazma ve kontrol servisleri (ECUReset,
# WriteDataByIdentifier, RoutineControl, SecurityAccess, ...) -f
# verilmedigi surece reddedilir.
#
# Lisans: MIT

set -uo pipefail

IFACE=slcan0
TX_ID=7E0
RX_ID=7E8
WAIT=2
FORCE=0

usage() { sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
die()   { echo "hata: $*" >&2; exit 1; }

while getopts ":i:t:r:w:fh" opt; do
    case "$opt" in
        i) IFACE="$OPTARG" ;;
        t) TX_ID="$OPTARG" ;;
        r) RX_ID="$OPTARG" ;;
        w) WAIT="$OPTARG" ;;
        f) FORCE=1 ;;
        h) usage 0 ;;
        *) echo "bilinmeyen secenek: -$OPTARG" >&2; usage 1 ;;
    esac
done
shift $((OPTIND - 1))
[[ $# -ge 1 ]] || usage 1

COMMAND=$1
shift

# ------------------------------------------------------------------ #
# Onkosullar                                                          #
# ------------------------------------------------------------------ #

for tool in isotpsend isotprecv python3; do
    command -v "$tool" >/dev/null 2>&1 \
        || die "'$tool' bulunamadi. Kurulum: apt install can-utils python3"
done

ip link show "$IFACE" >/dev/null 2>&1 \
    || die "'$IFACE' arayuzu yok. Once: sudo ./scripts/slcan-up.sh"

if ! lsmod 2>/dev/null | grep -q '^can_isotp'; then
    if ! modinfo can-isotp >/dev/null 2>&1; then
        die "can-isotp cekirdek modulu yok (Linux 5.10+ gerekir)"
    fi
    echo "==> can-isotp modulu yukleniyor (sudo gerekebilir)" >&2
    sudo modprobe can-isotp || die "can-isotp yuklenemedi"
fi

# ------------------------------------------------------------------ #
# Guvenlik: yazma ve kontrol servisleri                               #
# ------------------------------------------------------------------ #

# Aracin durumunu degistirebilecek UDS servisleri. Bunlar yanlis
# kullanildiginda ECU'yu kilitleyebilir, ariza kaydini silebilir veya
# aracı hareket ettirebilir.
is_dangerous_service() {
    case "${1^^}" in
        11) echo "ECUReset - ECU'yu yeniden baslatir" ;;
        14) echo "ClearDiagnosticInformation - ariza kayitlarini SILER" ;;
        27) echo "SecurityAccess - guvenlik kilidini acmayi dener" ;;
        28) echo "CommunicationControl - ECU haberlesmesini kapatabilir" ;;
        2E) echo "WriteDataByIdentifier - ECU'ya kalici veri YAZAR" ;;
        2F) echo "InputOutputControl - aktuatorleri dogrudan surer" ;;
        31) echo "RoutineControl - ECU rutini calistirir" ;;
        34|35|36|37) echo "Download/Upload - ECU yazilimina dokunur" ;;
        3D) echo "WriteMemoryByAddress - ECU belleğine YAZAR" ;;
        85) echo "ControlDTCSetting - ariza kaydini durdurur" ;;
        *)  return 1 ;;
    esac
    return 0
}

guard_service() {
    local svc=$1 why
    if why=$(is_dangerous_service "$svc"); then
        if [[ $FORCE -ne 1 ]]; then
            echo "reddedildi: servis 0x${svc^^} - $why" >&2
            echo "            gercekten istiyorsaniz -f ekleyin." >&2
            exit 3
        fi
        echo "UYARI: servis 0x${svc^^} - $why" >&2
        echo "       -f verildi, devam ediliyor." >&2
    fi
}

# ------------------------------------------------------------------ #
# Taşıma                                                              #
# ------------------------------------------------------------------ #

# request <hexstring> -> cevabi hex olarak stdout'a yazar, yoksa bos
request() {
    local payload=${1// /}
    local out rc=0

    # isotprecv once baslatilir; aksi halde ECU'nun cevabi kacirilir.
    out=$(mktemp)
    isotprecv -s "$TX_ID" -d "$RX_ID" "$IFACE" >"$out" 2>/dev/null &
    local recv_pid=$!
    sleep 0.2

    if ! printf '%s' "$payload" | isotpsend -s "$TX_ID" -d "$RX_ID" "$IFACE" 2>/dev/null; then
        kill "$recv_pid" 2>/dev/null
        wait "$recv_pid" 2>/dev/null
        rm -f "$out"
        return 1
    fi

    # Cevabi bekle. Onda bir saniyelik adimlarla, tamsayi aritmetigiyle:
    # 'bc' her sistemde kurulu olmayabiliyor.
    local ticks=$((WAIT * 10)) waited=0
    while (( waited < ticks )); do
        [[ -s "$out" ]] && break
        sleep 0.1
        waited=$((waited + 1))
    done

    kill "$recv_pid" 2>/dev/null
    wait "$recv_pid" 2>/dev/null

    if [[ -s "$out" ]]; then
        tr -d ' \n' <"$out"
    else
        rc=1
    fi
    rm -f "$out"
    return $rc
}

# ------------------------------------------------------------------ #
# Cozumleyiciler                                                      #
# ------------------------------------------------------------------ #

decode() {
    python3 "$(dirname "$0")/udsdecode.py" "$@"
}

# ------------------------------------------------------------------ #
# Komutlar                                                            #
# ------------------------------------------------------------------ #

echo "arayuz: $IFACE   tester: 0x${TX_ID^^} -> ECU: 0x${RX_ID^^}"
echo

case "$COMMAND" in
    vin)
        echo "== VIN (UDS 22 F190) =="
        if resp=$(request "22F190"); then
            decode vin "$resp"
        else
            echo "UDS cevabi yok, OBD-II mode 09 PID 02 deneniyor..."
            if resp=$(request "0902"); then
                decode vin "$resp"
            else
                echo "cevap alinamadi"
                exit 1
            fi
        fi
        ;;

    dtc)
        echo "== Kayitli ariza kodlari (UDS 19 02) =="
        if resp=$(request "1902FF"); then
            decode dtc "$resp"
        else
            echo "UDS cevabi yok, OBD-II mode 03 deneniyor..."
            if resp=$(request "03"); then
                decode dtc "$resp"
            else
                echo "cevap alinamadi"
                exit 1
            fi
        fi
        ;;

    ident)
        echo "== ECU kimlik bilgileri =="
        # F186 aktif oturum, F187 parca no, F189 yazilim surumu,
        # F18C ECU seri no, F190 VIN, F194 yazilim parca no
        for did in F186 F187 F188 F189 F18A F18C F191 F194 F195; do
            if resp=$(request "22$did"); then
                decode did "$did" "$resp"
            fi
        done
        ;;

    session)
        echo "== Tanilama oturumu (10 01 - varsayilan) =="
        if resp=$(request "1001"); then
            decode raw "$resp"
        else
            echo "cevap alinamadi"
            exit 1
        fi
        ;;

    scan)
        echo "== Cevap veren ECU'lar araniyor (7E0-7E7) =="
        found=0
        for i in 0 1 2 3 4 5 6 7; do
            TX_ID=$(printf '7E%X' "$i")
            RX_ID=$(printf '7E%X' $((i + 8)))
            printf '  0x%s -> 0x%s ... ' "$TX_ID" "$RX_ID"
            # 3E 00 = TesterPresent: en zararsiz UDS istegi.
            if resp=$(request "3E00"); then
                echo "CEVAP VAR  ($resp)"
                found=$((found + 1))
            else
                echo "-"
            fi
        done
        echo
        echo "$found ECU cevap verdi."
        ;;

    raw)
        [[ $# -ge 1 ]] || die "raw icin hex istek gerekli, orn: raw 22F190"
        payload=${1// /}
        [[ "$payload" =~ ^[0-9A-Fa-f]+$ ]] || die "gecersiz hex: $1"
        [[ $((${#payload} % 2)) -eq 0 ]] || die "hex uzunlugu cift olmali"
        guard_service "${payload:0:2}"

        echo "== Ham istek: ${payload^^} =="
        if resp=$(request "$payload"); then
            decode raw "$resp"
        else
            echo "cevap alinamadi"
            exit 1
        fi
        ;;

    *)
        die "bilinmeyen komut: $COMMAND (yardim icin -h)"
        ;;
esac
