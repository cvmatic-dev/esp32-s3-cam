#include "sdcard.h"
#include "board_pins.h"

#include <stdio.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

static const char *TAG = "sdcard";
static sdmmc_card_t *s_card;

esp_err_t app_sd_mount(void)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false, /* never destroy user data */
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        s_card = NULL;
        return err;
    }
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

bool app_sd_mounted(void)
{
    return s_card != NULL;
}

uint64_t app_sd_size_mb(void)
{
    if (!s_card) {
        return 0;
    }
    return ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024);
}

esp_err_t app_sd_write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "short write %u/%u to %s", (unsigned)written, (unsigned)len, path);
        return ESP_FAIL;
    }
    return ESP_OK;
}
