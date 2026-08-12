#!/usr/bin/env bash
#
# slcan-down.sh - Tears down what slcan-up.sh created.
#
# Usage:
#   sudo ./scripts/slcan-down.sh [-i IFACE]

set -uo pipefail

IFACE=slcan0
PTY_LINK=/run/slcan-pty
STATE_DIR=/run/slcan-adapter

while getopts ":i:h" opt; do
    case "$opt" in
        i) IFACE="$OPTARG" ;;
        h) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: -$OPTARG" >&2; exit 1 ;;
    esac
done

if [[ $EUID -ne 0 ]]; then
    echo "error: must be run as root" >&2
    exit 1
fi

if ip link show "$IFACE" >/dev/null 2>&1; then
    echo "==> bringing $IFACE down"
    ip link set "$IFACE" down || true
fi

# slcand closes the CAN channel itself thanks to its -c flag, which is
# what puts the MCP2515 back into configuration mode and stops it
# acknowledging frames on the vehicle bus. Give it a moment to do so.
if pgrep -f "slcand .* ${IFACE}\$" >/dev/null 2>&1; then
    echo "==> stopping slcand"
    pkill -f "slcand .* ${IFACE}\$" || true
    sleep 0.5
fi

if [[ -f "$STATE_DIR/socat.pid" ]]; then
    SOCAT_PID=$(cat "$STATE_DIR/socat.pid")
    if kill -0 "$SOCAT_PID" 2>/dev/null; then
        echo "==> stopping socat (pid $SOCAT_PID)"
        kill "$SOCAT_PID" 2>/dev/null || true
    fi
    rm -f "$STATE_DIR/socat.pid"
fi

rm -f "$PTY_LINK"
echo "    done."
