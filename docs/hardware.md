# Hardware notes

> Fill in the "verified" column once `make test` passes on the real board.
> Agents: treat unverified rows as assumptions.

## Board
| Item | Value | Verified |
|---|---|---|
| Board / vendor | ESP32-S3-CAM (vendor? Freenove / Waveshare / clone) | ☐ |
| Module | ESP32-S3-WROOM-1 N16R8 (assumed) | ☐ |
| Flash | 8 MB configured (16 MB if N16R8) | ☐ |
| PSRAM | 8 MB **octal** (assumed) | ☐ |
| Sensor | OV5640, 24-pin FPC, PID 0x5640 | ☐ |
| SD | microSD, SDMMC 1-bit | ☐ |
| USB to host | native USB (303a:1001) → `/dev/esp32cam-jtag` | ☐ |
| Second USB | CH343 UART → `/dev/esp32cam-uart` (if present) | ☐ |
| Host | dellws (Linux) | ☐ |

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
- (add dated entries: what was observed, what fixed it)
