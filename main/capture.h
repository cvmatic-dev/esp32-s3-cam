#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * Capture + stream utility (CONFIG_APP_MODE_CAPTURE).
 *
 * Grayscale (Y-only) 720p capture with a fixed manual exposure and AEC/AGC/AWB
 * off. Each frame is encoded to a grayscale JPEG by the ESP32-S3 hardware
 * encoder, written to SD, and published as "latest frame" for the MJPEG stream.
 */

/* Bring up the camera, calibrate frame timing, pick a framesize that holds the
 * target fps, and start the capture + SD-writer tasks. On success it prints the
 * @@CHECK camera/exposure markers and a @@STATS line. */
esp_err_t app_capture_start(void);

/* Copy the most recent encoded JPEG into dst (cap >= app_capture_jpeg_capacity()).
 * Returns false if no frame is ready yet; *seq increments once per published frame. */
bool app_capture_latest(uint8_t *dst, size_t cap, size_t *len, uint32_t *seq);

/* Size the caller must provide to app_capture_latest(). */
size_t app_capture_jpeg_capacity(void);
