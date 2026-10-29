# ADR 0001 — ESP-IDF (FreeRTOS) instead of Zephyr

Status: accepted · 2026-10-29 (amended: IDF v6.0.3, see Consequences)

## Context
Target: ESP32-S3 + OV5640 (DVP) + microSD, image capture/streaming, developed
mostly by coding agents on a host with the board attached over USB.

## Decision
Use ESP-IDF v6.0.3 (FreeRTOS kernel) with the `espressif/esp32-camera`
managed component.

## Why
- **Camera path is the core of the product.** `esp32-camera` is Espressif's
  maintained driver for OV5640 on ESP32-S3 (JPEG in hardware on the sensor,
  PSRAM frame buffers, grab modes, sensor tuning API) and is used by
  thousands of S3-CAM projects. Zephyr has an ESP32 LCD_CAM DVP binding and
  an OV5640 driver, but that combination is young on this SoC; most Zephyr
  OV5640 usage is MIPI on NXP i.MX RT. Debugging a camera pipeline on a
  less-trodden path costs far more than any RTOS benefit.
- **Agent productivity.** Far more public ESP-IDF code/examples/issues exist
  for this board class, so agents produce correct code more often. One build
  tool (`idf.py`), one config file, one component manager.
- **Ecosystem:** Wi-Fi/BLE, HTTP server (MJPEG streaming), OTA, esp-dl/esp-who
  for on-device vision, USB host/device — all first-party.
- **Tooling:** built-in USB-Serial-JTAG gives flashing + console + JTAG debug
  over one cable.

## When Zephyr would be the better choice
- Portability to non-Espressif MCUs (nRF, STM32, NXP) is a requirement.
- You want devicetree-driven board abstraction, Zephyr's test framework
  (twister, native_sim) or its networking/security certification story.
- The camera is not central (e.g. sensor node that occasionally snaps).

## Consequences
- FreeRTOS APIs (tasks, queues, event groups) and ESP-IDF drivers are used
  directly; no portability layer.
- Revisit when moving to ESP-IDF v6.x (removes some legacy drivers; check
  `esp32-camera` compatibility first).
- **Amendment (2026-10-29):** moved to ESP-IDF v6.0.3. `esp32-camera` 2.1.8
  builds and runs on v6.0.3 — camera probe, JPEG frames and the boot
  self-test all pass on the real board (`build/hw/result.json` verdict PASS).
