#pragma once
#include "esp_err.h"
#include "esp_camera.h"

/* Initialise OV5640 on the DVP port using board_pins.h + Kconfig settings.
 * Returns ESP_ERR_NOT_FOUND if a sensor answers but it is not an OV5640. */
esp_err_t app_camera_init(void);

/* Grab and immediately return `n` frames so AEC/AWB can settle. */
void app_camera_warmup(int n);

/* Sensor PID (e.g. 0x5640) or 0 if not initialised. */
uint16_t app_camera_pid(void);
