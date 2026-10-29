#!/usr/bin/env bash
# The agent's main loop: build -> flash -> reset -> capture log -> verdict.
#
#   scripts/test_hw.sh                 # full cycle
#   SKIP_BUILD=1 scripts/test_hw.sh    # reflash current build
#   NO_FLASH=1 scripts/test_hw.sh      # just reset + observe
#   TIMEOUT=60 scripts/test_hw.sh      # longer observation window
#   APP_ONLY=1 scripts/test_hw.sh      # flash only the app partition (faster)
#
# Artifacts (overwritten each run):
#   build/hw/serial.log    full device log, timestamped, ANSI-stripped
#   build/hw/result.json   parsed checks / frames / stats / panic / backtrace
#   build/hw/images/*.jpg  frames dumped by the firmware over serial
# Exit: 0 PASS, 1 FAIL/panic, 2 timeout, 3 port/toolchain problem, other = build/flash error
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "${SKIP_BUILD:-0}" != "1" ] && [ "${NO_FLASH:-0}" != "1" ]; then
    scripts/build.sh
fi

# shellcheck source=scripts/env.sh
. scripts/env.sh
mapfile -t P < <(python3 tools/esp_monitor.py --resolve-port)
[ "${#P[@]}" -eq 2 ] || { echo "TEST: no ESP32 serial port found (see: make ports)"; exit 3; }
PORT="${P[0]}"; LOCK="${P[1]}"

rm -rf build/hw && mkdir -p build/hw/images

# Hold one lock across flash + monitor so no other agent sneaks in between.
exec 9>"$LOCK"
flock -w 120 9 || { echo "TEST: port $PORT busy (lock $LOCK)"; exit 3; }
export ESP_PORT_LOCK_HELD=1 ESP_PORT="$PORT"

if [ "${NO_FLASH:-0}" != "1" ]; then
    if [ "${APP_ONLY:-0}" = "1" ]; then scripts/flash.sh --app-only; else scripts/flash.sh; fi
fi

set +e
python3 tools/esp_monitor.py \
    --port "$PORT" \
    --timeout "${TIMEOUT:-40}" \
    --log build/hw/serial.log \
    --json build/hw/result.json \
    --save-images build/hw/images \
    --elf build/esp32s3_cam.elf \
    ${QUIET:+--quiet}
rc=$?
set -e
echo "TEST: exit $rc (0=PASS 1=FAIL 2=TIMEOUT 3=PORT) — see build/hw/"
exit "$rc"
