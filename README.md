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

## Capture + stream utility (`make capture`)

A second app mode (`CONFIG_APP_MODE_CAPTURE`) for collecting video for analysis:
720p **grayscale**, fixed manual exposure (**1/32 s**), with AEC, AGC, AWB and
the 50/60 Hz banding filter all **off**. It records to SD and serves a live MJPEG
preview. The boot self-test stays the default mode.

```bash
make capture        # build + flash + run it
```

- **SoftAP:** SSID `esp32cam`, password `esp32cam-stream` (both configurable in
  `menuconfig`). Join from a phone and open <http://192.168.4.1/> to watch the
  stream (~5 fps). Android may warn "no internet" — the page still works.
- **Recording:** `/sdcard/cap/run_NNN/video.mjpeg` — concatenated grayscale JPEG
  frames. One sequential file (not one file per frame): per-file writes on FAT32
  cost ~60 ms each and cap the card at ~15 fps; sequential puts it at ~25 fps.
  Split on `FFD8`/`FFD9` on the PC, or open directly with ffmpeg.
- Measured on this N16R8 board: **~25 fps** at 1280×720, ~22 KB/frame,
  `aec=0 agc=0 awb=0`. Reported in the `@@CHECK camera/exposure/wifi/stream`
  lines and a `@@STATS` line every 5 s.

Knobs (`menuconfig` → "App (esp32s3-cam)"): framesize, target fps, exposure
numerator/denominator, JPEG quality, WiFi SSID/password/channel. 720p tops out
at ~24–25 fps on this DVP/PSRAM path, so shortening VTS is not attempted; if a
framesize is far below the target the code steps down (`APP_CAPTURE_AUTOSHRINK`).

## Why ESP-IDF, not Zephyr
See `docs/decisions/0001-esp-idf-over-zephyr.md`.
