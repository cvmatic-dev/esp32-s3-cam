#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Mount the microSD card (SDMMC 1-bit) at SD_MOUNT_POINT. Never formats. */
esp_err_t app_sd_mount(void);
bool app_sd_mounted(void);
uint64_t app_sd_size_mb(void);

/* Write a buffer to `path` (absolute, under SD_MOUNT_POINT). */
esp_err_t app_sd_write_file(const char *path, const uint8_t *data, size_t len);
