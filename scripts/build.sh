#!/usr/bin/env bash
# Build the firmware. Exit code = build result.
# On failure prints a compact list of compiler errors at the end.
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck source=scripts/env.sh
. scripts/env.sh

mkdir -p build
LOG=build/last_build.log

if idf.py build >"$LOG" 2>&1; then
    tail -n 5 "$LOG"
    idf.py size 2>/dev/null | grep -E "Total image size|Used static (IRAM|DRAM)" || true
    echo "BUILD: OK"
else
    rc=$?
    echo "BUILD: FAILED (exit $rc). Full log: $LOG"
    echo "---- first errors ----"
    grep -nE "(error|Error|ERROR)[: ]|undefined reference|CMake Error" "$LOG" | head -n 40 || tail -n 40 "$LOG"
    exit "$rc"
fi
