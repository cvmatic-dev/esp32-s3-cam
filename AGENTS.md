# AGENTS.md — esp32s3-cam

Firmware for an ESP32-S3-CAM board (OV5640 on DVP, microSD on SDMMC 1-bit,
8 MB octal PSRAM) built with **ESP-IDF (FreeRTOS)**. The board is attached by
USB to this host (`dellws`). You can build, flash and observe it yourself.

## The loop (use this, not raw idf.py)

```bash
make test        # build -> flash -> reset -> self-test; exit 0 = PASS
```

| Exit | Meaning | What to do |
|---|---|---|
| 0 | `@@RESULT PASS` | done; read `build/hw/result.json` for numbers |
| 1 | `@@RESULT FAIL`, panic, reboot loop, corrupt image dump | read `fail_reason`, `panic`, `decoded_backtraces` in `build/hw/result.json`, then `build/hw/serial.log` |
| 2 | timeout, no verdict | firmware hung or never booted; check `serial.log` tail |
| 3 | port/toolchain problem | `make ports`; if nothing listed → **ask the human** (cable/board) |
| other | build or flash error | build: `build/last_build.log`; flash: `build/last_flash.log` |

Other targets: `make build`, `make flash`, `make observe` (reset + watch
without reflashing), `make ports`, `make reconfigure`, `make size`.
Knobs: `TIMEOUT=60`, `SKIP_BUILD=1`, `APP_ONLY=1`, `QUIET=1` before `make test`
or `scripts/test_hw.sh`.

**Look at the picture.** Every self-test dumps its last frame over serial;
it lands in `build/hw/images/selftest_last.jpg`. Open it with your image
viewing tool after any camera-related change — "frames arrived" is not the
same as "image is correct" (check exposure, colour, orientation, tearing).

Never run `idf.py monitor` (interactive, never exits). Use
`tools/esp_monitor.py` — see `--help` (`--until REGEX`, `--no-reset`,
`--timeout`, `--json`).

## Log protocol (firmware ⇄ tools)

Lines starting with `@@` are a contract parsed by `tools/esp_monitor.py`.
Keep them stable; add new keys rather than renaming. Format:
`@@TAG key=value key=value` (values contain no spaces).

| Marker | When |
|---|---|
| `@@BOOT fw= ver= idf=` | first line of app_main |
| `@@CHECK <subsystem>=OK\|FAIL ...` | one per subsystem (psram, sd, camera) |
| `@@FRAME i= w= h= len= jpeg=OK\|BAD` | each captured frame |
| `@@STATS ...` / `@@HEAP ...` | after the test |
| `@@RESULT PASS` / `@@RESULT FAIL reason=<token>` | exactly once per boot; ends the test |
| `@@IMG_BEGIN/@@IMG:/@@IMG_END` | binary dump (see `main/frame_dump.h`) |

New features should add a `@@CHECK` and keep `@@RESULT` truthful. If you add
a long-running mode, print `@@RESULT` before entering the loop.

## Layout

```
main/main.c          app_main + boot self-test (emits markers)
main/camera.[ch]     OV5640 init via espressif/esp32-camera
main/sdcard.[ch]     SDMMC 1-bit mount at /sdcard (never formats)
main/frame_dump.[ch] base64 frame dump over the console
main/board_pins.h    ALL pin numbers live here and only here
main/Kconfig.projbuild  app options (frame size, quality, ...)
sdkconfig.defaults   config source of truth (sdkconfig is generated)
main/idf_component.yml + dependencies.lock   pinned managed components
tools/esp_monitor.py non-interactive monitor / verdict / image extraction
scripts/*.sh         build / flash / test_hw / env / setup_host
docs/hardware.md     board facts, verified pin map, known quirks
docs/decisions/      ADRs (why ESP-IDF, etc.)
```

## Rules

1. **Config:** change `sdkconfig.defaults` (or `Kconfig.projbuild`), then
   `make reconfigure`. Never hand-edit or commit `sdkconfig`.
2. **Pins:** only in `main/board_pins.h`. Never use GPIO19/20 (native USB —
   you would lose the port you flash through), GPIO0/45/46 (straps),
   GPIO35–37 (octal PSRAM).
3. **Never** run `espefuse.py`, `idf.py efuse-*`, secure-boot or
   flash-encryption commands. eFuses are one-time and can brick the board.
   `erase-flash` only if the human asks.
4. **Memory:** frame buffers live in PSRAM (`CAMERA_FB_IN_PSRAM`). DMA/ISR
   buffers must be internal (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`). Watch
   `@@HEAP min_internal=` — keep it above ~40 KB.
5. **Tasks:** camera driver runs on core 0 (`CONFIG_CAMERA_CORE0`); put app
   work on core 1 with `xTaskCreatePinnedToCore`. No blocking work in ISRs.
   Every task gets an explicit stack size; stack overflow → panic → exit 1.
6. **Errors:** use `ESP_ERROR_CHECK` only where failure is truly fatal;
   otherwise return `esp_err_t` and report via `@@CHECK`/`@@RESULT`.
7. **Components:** add with `idf.py add-dependency "ns/name^x.y"`; commit the
   updated `dependencies.lock`. Prefer Espressif-maintained components.
8. **Physical actions** (replug cable, press BOOT/RST, insert SD card, change
   lens/scene) — you can't do them. Stop and ask the human, saying exactly
   what to do and why.
9. **SD card** contents persist across flashes; the self-test writes
   `/sdcard/cap/img_NNNNN.jpg`. Don't format it.
10. Before claiming a hardware change works: `make test` must exit 0 **and**
    you have looked at `build/hw/images/selftest_last.jpg`.

## Troubleshooting map

| Symptom in log | Likely cause |
|---|---|
| `PSRAM ID read error`, boot loop at start | module has quad PSRAM: set `CONFIG_SPIRAM_MODE_QUAD=y` |
| `Camera probe failed`, `SCCB` errors | SIOD/SIOC/XCLK pins or ribbon cable seating |
| init OK but `FRAME status=TIMEOUT` | VSYNC/PCLK/data pins, or XCLK too high; try 10 MHz |
| `jpeg=BAD`, green/striped image | PCLK edge/timing, XCLK; lower frame size first |
| `sd=FAIL` with `0x107` / timeout | no card, wrong CMD/CLK/D0, or card not FAT32 |
| `Brownout detector was triggered` | USB port/cable can't supply camera peak current |
| `FLASH: FAILED ... Failed to connect` | board needs download mode (human: hold BOOT, tap RST) |
| port vanished after flash | firmware touched GPIO19/20 or crashed USB; human presses RST |
