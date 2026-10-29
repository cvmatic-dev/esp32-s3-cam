# esp32s3-cam

ESP32-S3-CAM + OV5640 + microSD firmware on **ESP-IDF v6.0.3 (FreeRTOS)**,
set up so coding agents (Claude Code, Codex, etc.) can build, flash and
verify on real hardware without a human in the loop.

## One-time setup on dellws (human)

```bash
git init && git add -A && git commit -m "scaffold"
scripts/setup_host.sh          # apt deps, udev rules, dialout group, ESP-IDF v6.0.3
# log out / in once (group change)
```

1. Check `main/board_pins.h` against your board's schematic (the default is
   the common Freenove-style ESP32-S3-CAM pinout).
2. Insert a FAT32 microSD card.
3. Plug the board's **native USB** port (labelled USB / OTG, not COM) into dellws.
4. `make ports` → should list `/dev/esp32cam-jtag`.
5. `make test` → expect `@@RESULT PASS` and `build/hw/images/selftest_last.jpg`.

If step 5 fails on PSRAM or camera probe, see the troubleshooting table in
`AGENTS.md`. Record what you verified in `docs/hardware.md`.

## Then hand it to the agent

Agents read `AGENTS.md` (Claude Code reads `CLAUDE.md`, which imports it).
The contract is: `make test` → exit code + `build/hw/result.json` +
`build/hw/serial.log` + `build/hw/images/`.

Remote agents: run the agent *on dellws* (e.g. over SSH/tmux), since the
board is only reachable there.

## Why ESP-IDF, not Zephyr
See `docs/decisions/0001-esp-idf-over-zephyr.md`.
