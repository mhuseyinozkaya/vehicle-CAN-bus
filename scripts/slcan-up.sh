#!/usr/bin/env bash
#
# slcan-up.sh - Brings up a SocketCAN interface backed by the Arduino
# SLCAN adapter.
#
# Why the socat detour: connecting slcand straight to the real TTY was
# tried and did not work reliably. slcand's -S option and the DTR/RTS
# handling fight with the Arduino's auto-reset circuit, so the channel
# opens but commands are never processed. Bridging the real port to a
# PTY with socat sidesteps both problems:
#
#   [Arduino/MCP2515] <--USB--> [socat] <--PTY--> [slcand] <--> slcan0
#
# Usage:
#   sudo ./scripts/slcan-up.sh [-d DEVICE] [-b BAUD] [-s SPEED] [-i IFACE] [-a]
#
#   -d DEVICE   serial device                (default: autodetected)
#   -b BAUD     serial baud rate, must match SERIAL_BAUDRATE in config.h
#                                            (default: 115200)
#   -s SPEED    slcand CAN bitrate digit 0-8 (default: 6 = 500 kbit/s)
#   -i IFACE    SocketCAN interface name     (default: slcan0)
#   -a          autodetect the bitrate before attaching slcand. The scan
#               is listen-only, so it cannot disturb the vehicle bus.
#
# Run ./scripts/slcan-down.sh to tear everything down again.

set -euo pipefail

DEVICE=""
BAUD=115200
CAN_SPEED=6
IFACE=slcan0
AUTODETECT=0

PTY_LINK=/run/slcan-pty
STATE_DIR=/run/slcan-adapter

usage() { sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while getopts ":d:b:s:i:ah" opt; do
    case "$opt" in
        d) DEVICE="$OPTARG" ;;
        b) BAUD="$OPTARG" ;;
        s) CAN_SPEED="$OPTARG" ;;
        i) IFACE="$OPTARG" ;;
        a) AUTODETECT=1 ;;
        h) usage 0 ;;
        *) echo "unknown option: -$OPTARG" >&2; usage 1 ;;
    esac
done

die() { echo "error: $*" >&2; exit 1; }

# ---------------------------------------------------------------- #
# Preflight                                                         #
# ---------------------------------------------------------------- #

[[ $EUID -eq 0 ]] || die "must be run as root (slcand and 'ip link' need it)"

for tool in socat slcand ip; do
    command -v "$tool" >/dev/null 2>&1 \
        || die "'$tool' not found. Install with: apt install can-utils socat iproute2"
done

[[ "$CAN_SPEED" =~ ^[0-8]$ ]] || die "CAN speed digit must be 0-8, got '$CAN_SPEED'"

# Autodetect the Arduino if no device was given.
if [[ -z "$DEVICE" ]]; then
    for candidate in /dev/ttyACM* /dev/ttyUSB*; do
        [[ -e "$candidate" ]] || continue
        DEVICE="$candidate"
        break
    done
    [[ -n "$DEVICE" ]] || die "no /dev/ttyACM* or /dev/ttyUSB* found; pass -d explicitly"
    echo "==> autodetected device: $DEVICE"
fi

[[ -c "$DEVICE" ]] || die "'$DEVICE' is not a character device"

if ip link show "$IFACE" >/dev/null 2>&1; then
    die "interface '$IFACE' already exists. Run scripts/slcan-down.sh first."
fi

mkdir -p "$STATE_DIR"

# ---------------------------------------------------------------- #
# 1. Bridge the real serial port to a PTY                           #
# ---------------------------------------------------------------- #

rm -f "$PTY_LINK"

echo "==> socat: $DEVICE (${BAUD} baud) <-> $PTY_LINK"
socat -d \
    "pty,raw,echo=0,link=${PTY_LINK},b${BAUD}" \
    "${DEVICE},raw,echo=0,b${BAUD},nonblock,cs8,parenb=0,cstopb=0" &
SOCAT_PID=$!
echo "$SOCAT_PID" > "$STATE_DIR/socat.pid"

cleanup_on_error() {
    kill "$SOCAT_PID" 2>/dev/null || true
    rm -f "$STATE_DIR/socat.pid" "$PTY_LINK"
}
trap cleanup_on_error ERR

# Wait for the symlink to appear.
for _ in $(seq 1 50); do
    [[ -e "$PTY_LINK" ]] && break
    sleep 0.1
done
[[ -e "$PTY_LINK" ]] || die "socat did not create $PTY_LINK"

# Opening the port toggles DTR, which resets the Arduino. Give the
# bootloader time to hand over to the sketch before talking to it -
# otherwise the first commands are swallowed by the bootloader.
echo "==> waiting for the board to finish resetting"
sleep 2

# ---------------------------------------------------------------- #
# 2. Optional: ask the firmware which bitrate the bus runs at       #
# ---------------------------------------------------------------- #

# Talks raw SLCAN to the adapter over the PTY, before slcand claims it.
# The firmware's 'B' command listens on each supported bitrate in turn -
# always in listen-only mode - and answers 'B<digit>' or BEL on failure.
autodetect_bitrate() {
    local reply

    echo "==> scanning for the bus bitrate (listen-only, ~2 s)"

    exec 3<>"$PTY_LINK" || return 1
    # Drain anything the board may have left in the buffer.
    timeout 0.3 cat <&3 >/dev/null 2>&1 || true

    printf 'C\r' >&3
    sleep 0.2
    printf 'B\r' >&3

    # Reply is at most 3 bytes ('B', digit, CR); allow the full scan time.
    reply=$(timeout 6 head -c 3 <&3 2>/dev/null | tr -d '\r') || true
    exec 3<&-
    exec 3>&-

    if [[ "$reply" =~ ^B([0-8])$ ]]; then
        CAN_SPEED="${BASH_REMATCH[1]}"
        echo "    detected: S${CAN_SPEED} ($(bitrate_name "$CAN_SPEED"))"
        return 0
    fi

    echo "    no bus traffic detected; falling back to S${CAN_SPEED}" >&2
    echo "    (is the adapter connected to a live bus with the ignition on?)" >&2
    return 1
}

bitrate_name() {
    case "$1" in
        0) echo "10 kbit/s"   ;; 1) echo "20 kbit/s"  ;;
        2) echo "50 kbit/s"   ;; 3) echo "100 kbit/s" ;;
        4) echo "125 kbit/s"  ;; 5) echo "250 kbit/s" ;;
        6) echo "500 kbit/s"  ;; 8) echo "1 Mbit/s"   ;;
        *) echo "unknown"     ;;
    esac
}

if [[ $AUTODETECT -eq 1 ]]; then
    autodetect_bitrate || true
fi

# ---------------------------------------------------------------- #
# 3. Attach slcand to the PTY                                       #
# ---------------------------------------------------------------- #

# -o open the channel, -c close it on exit, -s<n> set the bitrate.
echo "==> slcand: $PTY_LINK -> $IFACE (S${CAN_SPEED})"
slcand -o -c -f -s"${CAN_SPEED}" "$PTY_LINK" "$IFACE"

for _ in $(seq 1 50); do
    ip link show "$IFACE" >/dev/null 2>&1 && break
    sleep 0.1
done
ip link show "$IFACE" >/dev/null 2>&1 || die "slcand did not create $IFACE"

# ---------------------------------------------------------------- #
# 4. Bring the interface up                                         #
# ---------------------------------------------------------------- #

# A short TX queue keeps latency low; the default of 10 is very small
# for a bus that can burst.
ip link set "$IFACE" txqueuelen 1000
ip link set "$IFACE" up

trap - ERR

echo
echo "    $IFACE is up at S${CAN_SPEED} ($(bitrate_name "$CAN_SPEED"))."
echo
echo "    watch traffic:    candump $IFACE"
echo "    log for analysis: candump -l $IFACE"
echo "    compare two logs: ./tools/candiff.py diff a.log b.log"
echo "    diagnostics:      ./tools/uds.sh -i $IFACE vin"
echo "    tear down:        sudo ./scripts/slcan-down.sh -i $IFACE"
echo
echo "    For a read-only adapter that cannot write to the bus at all,"
echo "    set SLCAN_READ_ONLY 1 in slcan_firmware_uno/config.h and reflash."
