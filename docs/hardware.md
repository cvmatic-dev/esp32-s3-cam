# Hardware notes

> Verified against the boot log of the last green self-test
> (`build/hw/result.json`, `build/hw/serial.log`, 2026-10-29).
> Rows still marked ☐ are assumptions — do not rely on them.

## Board
| Item | Value | Verified |
|---|---|---|
| Board / vendor | ESP32-S3-CAM, clone (silkscreen vendor not yet read) | ☐ |
| Module | ESP32-S3-WROOM-1 **N16R8** (16 MB flash + 8 MB octal PSRAM) | ✓ |
| Chip | ESP32-S3, chip revision v0.2 | ✓ |
| Flash | **16 MB** physical (boya chip) — currently only 8 MB configured | ✓ |
| PSRAM | 8 MB **octal**, APMemory (vendor 0x0d), gen 3, 80 MHz | ✓ |
| Sensor | OV5640, 24-pin FPC, PID 0x5640 | ✓ |
| SD | microSD 58 GB ("59638 MB"), SDMMC 1-bit, FAT32 | ✓ |
| USB to host | **CH343 UART** → `/dev/ttyACM0` (`/dev/esp32cam-uart`) | ✓ |
| Native USB | 303a:1001 NOT enumerated at test time (`/dev/esp32cam-jtag` absent) | ✓ |
| Second USB | CH343 UART on the "COM" USB-C (used for flash + logs) | ✓ |
| Other serial | Silabs CP210x on `/dev/ttyUSB0` sn=0001 — likely unrelated device | ✓ |
| Host | dellws (Linux) | ✓ |

## Pin map (mirror of `main/board_pins.h`)
| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| XCLK | 15 | | D7/Y9 | 16 |
| SIOD (SDA) | 4 | | D6/Y8 | 17 |
| SIOC (SCL) | 5 | | D5/Y7 | 18 |
| VSYNC | 6 | | D4/Y6 | 12 |
| HREF | 7 | | D3/Y5 | 10 |
| PCLK | 13 | | D2/Y4 | 8 |
| PWDN | – | | D1/Y3 | 9 |
| RESET | – | | D0/Y2 | 11 |
| SD CLK | 39 | | SD CMD | 38 |
| SD D0 | 40 | | | |

## Quirks / findings
- **2026-10-29** — Self-test passes end to end: psram OK (8192 KB), camera OK
  (OV5640 pid=0x5640), 5× 800×600 JPEG frames, `min_internal=` 309 KB heap.
  Flash+reset+logs currently go over the **CH343 UART** (`/dev/esp32cam-uart`),
  not native USB-JTAG.
- **2026-10-29** — Flash chip is **16 MB** but `sdkconfig.defaults` selects
  8 MB, so the bootloader warns *"Detected size(16384k) larger than the size
  in the binary image header(8192k)"*. Switch to 16 MB (and grow the partition
  table) to use the full flash and make the warning go away.
- **2026-10-29** — Default image is underexposed with a magenta/purple AWB
  cast (see image-quality tuning in `main/camera.c`); camera path itself works.
