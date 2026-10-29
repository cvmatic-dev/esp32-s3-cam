---
description: Build, flash and run the on-board self-test, then report what the camera saw
---
1. Run `make test` (add `TIMEOUT=60` if the change makes boot slower).
2. Read `build/hw/result.json`. Report: verdict, each `checks.*` status, frame size/fps from `stats`, `heap.min_internal`.
3. If `images` is non-empty, open `build/hw/images/selftest_last.jpg` and describe it in one or two sentences (exposure, colour cast, orientation, artefacts).
4. On failure: quote `fail_reason` / `panic` / `decoded_backtraces`, show the last ~30 lines of `build/hw/serial.log`, and give your best diagnosis using the troubleshooting map in AGENTS.md. If a physical action is needed, say exactly what the human should do.
$ARGUMENTS
