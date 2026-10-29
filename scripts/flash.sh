#!/usr/bin/env bash
# Flash bootloader + partition table + app. Holds the serial-port lock.
# Usage: scripts/flash.sh [--app-only]
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
# shellcheck source=scripts/env.sh
. scripts/env.sh

TARGET=flash
[ "${1:-}" = "--app-only" ] && TARGET=app-flash

mapfile -t P < <(python3 tools/esp_monitor.py --resolve-port)
[ "${#P[@]}" -eq 2 ] || { echo "FLASH: no ESP32 serial port found (see: make ports)"; exit 3; }
PORT="${P[0]}"; LOCK="${P[1]}"
BAUD="${ESP_FLASH_BAUD:-921600}"

if [ "${ESP_PORT_LOCK_HELD:-0}" = "1" ]; then
    run() { "$@"; }
else
    run() { flock -w 60 "$LOCK" "$@"; }
fi

if run idf.py -p "$PORT" -b "$BAUD" "$TARGET" >build/last_flash.log 2>&1; then
    grep -E "Hash of data verified|Hard resetting" build/last_flash.log | tail -n 3
    echo "FLASH: OK ($PORT)"
else
    rc=$?
    tail -n 25 build/last_flash.log
    echo "FLASH: FAILED (exit $rc) on $PORT. Full log: build/last_flash.log"
    if grep -q "Failed to connect" build/last_flash.log; then
        echo "hint: put board in download mode — hold BOOT, tap RST, release BOOT — then retry." \
             "If it keeps failing, ask the human (physical action needed)."
    fi
    if grep -qiE "Permission denied|could not open port" build/last_flash.log; then
        echo "hint: port permissions — run scripts/setup_host.sh udev part, or add user to 'dialout'."
    fi
    exit "$rc"
fi
