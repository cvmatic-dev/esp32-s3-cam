#pragma once
/*
 * Board pin map — ESP32-S3-CAM (Freenove ESP32-S3-WROOM CAM / "ESP32-S3-EYE"-
 * compatible layout, the most common "ESP32-S3-CAM N16R8" clone pinout).
 *
 * !!! VERIFY AGAINST YOUR BOARD'S SCHEMATIC BEFORE TRUSTING CAMERA RESULTS !!!
 * Symptom of a wrong map: "Camera probe failed" / "SCCB: no ACK" (SIOD/SIOC
 * wrong) or init OK but frames time out (VSYNC/PCLK/data wrong).
 * Record the verified board in docs/hardware.md.
 *
 * Reserved — do not use for app GPIO:
 *   GPIO19/20  native USB D-/D+  (reusing them kills the flashing/log port)
 *   GPIO0      boot strap (BOOT button)
 *   GPIO35-37  octal PSRAM on N8R8/N16R8 modules
 *   GPIO45/46  strapping pins
 */

/* ---- DVP camera (OV5640, 24-pin FPC) ---- */
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   15
#define CAM_PIN_SIOD    4   /* SCCB SDA */
#define CAM_PIN_SIOC    5   /* SCCB SCL */
#define CAM_PIN_D7     16   /* a.k.a. Y9 */
#define CAM_PIN_D6     17
#define CAM_PIN_D5     18
#define CAM_PIN_D4     12
#define CAM_PIN_D3     10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0     11   /* a.k.a. Y2 */
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK   13

#define CAM_XCLK_HZ    20000000

/* ---- microSD (SDMMC, 1-bit bus) ---- */
#define SD_PIN_CLK     39
#define SD_PIN_CMD     38
#define SD_PIN_D0      40
#define SD_MOUNT_POINT "/sdcard"
