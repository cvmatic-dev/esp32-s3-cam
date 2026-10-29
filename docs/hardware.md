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
| Flash | **16 MB** (boya chip), flashed as 16 MB with a 2×OTA partition table | ✓ |
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
- **2026-10-29** — Flash is now configured as **16 MB** with a factory-less
  2-slot OTA layout (see `partitions.csv`: `otadata` + `ota_0`/`ota_1`, ~7.9 MB
  each). The bootloader size warning is gone and the app boots from `ota_0`.
- **2026-10-29** — **Camera images are magenta/purple-tinted and dark, and it
  is not an AWB software fault.** Evidence: the sensor's built-in colour-bar
  test pattern renders a near-neutral white bar (so the ISP/colour matrix is
  fine); all five OV5640 white-balance modes were applied (register readback
  confirmed `0x3406` 0→1 and R/B gains `0x3400`/`0x3404` changing) yet every
  mode still shows the cast. Most likely cause is the scene/optics: magenta
  ambient (RGB LED) lighting, or a missing/ineffective IR-cut filter.
  **Needs a human:** aim the camera at white paper under neutral white light
  (or check the lens for an IR-cut filter) to tell the two apart.
- **2026-10-29** — Capture utility (`make capture`) verified on hardware:
  1280×720 **grayscale** (OV5640 ISP "grayscale" effect, U=V=0x80 — confirmed
  true R≈G≈B, no magenta), manual exposure 1/32 s (`lines=578`, `trow_ns≈54076`),
  `aec=0 agc=0 awb=0`, **~25 fps**, ~22 KB/frame, SD recording keeping up
  (~25 fps to one sequential `video.mjpeg`). SoftAP `esp32cam` at 192.168.4.1,
  MJPEG stream at 5 fps.
  - The **ESP32-S3 has no hardware JPEG encoder** (only ROM JPEG *decode*), so
    grayscale JPEG comes from the *sensor*, not an on-chip encoder.
  - `720p@25` is at the pipeline limit: the driver's default HD timing gives
    ~24.2 fps and shortening VTS to force 25 trips `FB-OVF` (DVP/PSRAM overrun),
    so the utility runs at the native rate (~24–25 fps) rather than forcing VTS.
  - Per-frame SD files cost ~60 ms each (FAT32), capping the card at ~15 fps;
    a single sequential `.mjpeg` reaches ~25 fps.
  - FATFS only commits the directory entry (file size) on `fsync`/`close`, so
    `fflush` alone leaves the file reading back as **0 bytes**; the writer
    `fsync`s every 25 frames (verified: `disk_kb` grows in `@@STATS`).
