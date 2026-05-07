#!/usr/bin/env bash
# scripts/debug-input.sh
# Dump raw input events from a device — useful for finding your numpad's
# event node and verifying key codes before configuring t9numpad.
#
# Usage:  ./scripts/debug-input.sh [DEVICE]
#   DEVICE defaults to the first event device found that looks like a numpad.

set -euo pipefail

DEVICE="${1:-}"

if [[ -z "$DEVICE" ]]; then
    echo "==> Scanning /dev/input/event* for numpad candidates …"
    for ev in /dev/input/event*; do
        name="$(cat /sys/class/input/"${ev##*/}"/device/name 2>/dev/null || true)"
        if echo "$name" | grep -qi "numpad\|num pad\|keypad\|kp"; then
            echo "    Found: $ev  ($name)"
            DEVICE="$ev"
        fi
    done
fi

if [[ -z "$DEVICE" ]]; then
    echo "No numpad found automatically.  Specify one:"
    ls /dev/input/event*
    exit 1
fi

echo "==> Dumping events from $DEVICE  (Ctrl-C to stop)"
if command -v evtest &>/dev/null; then
    evtest "$DEVICE"
else
    echo "(install 'evtest' for decoded output; falling back to hexdump)"
    cat "$DEVICE" | xxd
fi
