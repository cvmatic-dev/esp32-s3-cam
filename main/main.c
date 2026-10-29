/*
 * esp32s3-cam — boot self-test + capture/stream utility.
 *
 * Every boot prints machine-readable markers (see AGENTS.md "Log protocol"):
 *   @@BOOT    firmware/IDF version
 *   @@CHECK   one line per subsystem (psram, sd, camera, wifi, ...)
 *   @@FRAME   one line per captured frame (self-test)
 *   @@STATS   fps / sizes
 *   @@HEAP    heap after the test
 *   @@RESULT  PASS | FAIL reason=<token>
 * tools/esp_monitor.py waits for @@RESULT and turns it into an exit code.
 *
 * CONFIG_APP_MODE_SELFTEST (default) runs the boot self-test.
 * CONFIG_APP_MODE_CAPTURE runs the fixed-exposure grayscale capture utility:
 * SD recording + WiFi SoftAP MJPEG stream (see capture.c / stream.c).
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_system.h"
#include "sdkconfig.h"

#include "board_pins.h"
#include "camera.h"
#include "sdcard.h"
#include "frame_dump.h"

#if CONFIG_APP_MODE_CAPTURE
#include "capture.h"
#include "stream.h"
#include "wifi_ap.h"
#endif

static const char *TAG = "app";

#if !CONFIG_APP_MODE_CAPTURE
static void result_fail(const char *reason)
{
    printf("@@RESULT FAIL reason=%s\n", reason);
    fflush(stdout);
}

static bool run_selftest(void)
{
    /* PSRAM */
    size_t psram = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    printf("@@CHECK psram=%s size_kb=%u\n", psram ? "OK" : "FAIL", (unsigned)(psram / 1024));
    if (!psram) {
        result_fail("no_psram");
        return false;
    }

    /* SD card: a missing card is reported but is not fatal for the camera. */
    bool sd_ok = (app_sd_mount() == ESP_OK);
    printf("@@CHECK sd=%s size_mb=%" PRIu64 "\n", sd_ok ? "OK" : "FAIL", app_sd_size_mb());
    if (sd_ok) {
        mkdir(SD_MOUNT_POINT "/cap", 0775);
    }
#if CONFIG_APP_REQUIRE_SD
    if (!sd_ok) {
        result_fail("sd_mount");
        return false;
    }
#endif

    /* Camera */
    esp_err_t err = app_camera_init();
    printf("@@CHECK camera=%s pid=0x%04x err=%s\n", err == ESP_OK ? "OK" : "FAIL",
           app_camera_pid(), esp_err_to_name(err));
    if (err != ESP_OK) {
        result_fail("camera_init");
        return false;
    }
    app_camera_warmup(CONFIG_APP_WARMUP_FRAMES);

    /* Capture */
    const int n = CONFIG_APP_SELFTEST_FRAMES;
    size_t total = 0, min_len = SIZE_MAX, max_len = 0;
    int saved = 0;
    int64_t t0 = esp_timer_get_time();

    for (int i = 0; i < n; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            printf("@@FRAME i=%d status=TIMEOUT\n", i);
            result_fail("frame_timeout");
            return false;
        }
        bool jpeg_ok = fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
        printf("@@FRAME i=%d w=%u h=%u len=%u jpeg=%s\n", i, (unsigned)fb->width,
               (unsigned)fb->height, (unsigned)fb->len, jpeg_ok ? "OK" : "BAD");

        total += fb->len;
        min_len = fb->len < min_len ? fb->len : min_len;
        max_len = fb->len > max_len ? fb->len : max_len;

#if CONFIG_APP_SAVE_TO_SD
        if (sd_ok) {
            char path[64];
            snprintf(path, sizeof(path), SD_MOUNT_POINT "/cap/img_%05d.jpg", i);
            if (app_sd_write_file(path, fb->buf, fb->len) == ESP_OK) {
                saved++;
            }
        }
#endif
#if CONFIG_APP_SERIAL_DUMP
        if (i == n - 1) {
            frame_dump("selftest_last.jpg", fb->buf, fb->len);
        }
#endif
        esp_camera_fb_return(fb);

        if (!jpeg_ok) {
            result_fail("bad_jpeg");
            return false;
        }
    }

    /* fps excludes the serial dump of the last frame only approximately. */
    int64_t dt_us = esp_timer_get_time() - t0;
    printf("@@STATS frames=%d avg_len=%u min_len=%u max_len=%u elapsed_ms=%lld fps_x10=%lld sd_saved=%d\n",
           n, (unsigned)(total / n), (unsigned)min_len, (unsigned)max_len,
           (long long)(dt_us / 1000), (long long)(n * 10000000LL / (dt_us ? dt_us : 1)), saved);

#if CONFIG_APP_SAVE_TO_SD
    if (sd_ok && saved != n) {
        result_fail("sd_write");
        return false;
    }
#endif
    return true;
}
#else /* CONFIG_APP_MODE_CAPTURE */

static void run_capture_mode(void)
{
    bool sd_ok = (app_sd_mount() == ESP_OK);
    printf("@@CHECK sd=%s size_mb=%" PRIu64 "\n", sd_ok ? "OK" : "FAIL", app_sd_size_mb());

    if (app_capture_start() != ESP_OK) {
        printf("@@RESULT FAIL reason=capture_init\n");
        fflush(stdout);
        return;
    }
    if (app_wifi_ap_start() != ESP_OK) {
        printf("@@CHECK wifi=FAIL\n");
        printf("@@RESULT FAIL reason=wifi\n");
        fflush(stdout);
        return;
    }
    printf("@@CHECK wifi=OK ssid=%s ip=%s ch=%d\n", CONFIG_APP_WIFI_SSID,
           app_wifi_ap_ip(), CONFIG_APP_WIFI_CHANNEL);

    if (app_stream_start() != ESP_OK) {
        printf("@@CHECK stream=FAIL\n");
        printf("@@RESULT FAIL reason=stream\n");
        fflush(stdout);
        return;
    }
    printf("@@CHECK stream=OK port=80 fps=%d\n", CONFIG_APP_CAPTURE_STREAM_FPS);

    printf("@@RESULT PASS\n");
    fflush(stdout);
    ESP_LOGI(TAG, "capture running — browse http://%s/", app_wifi_ap_ip());
}
#endif

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    printf("@@BOOT fw=%s ver=%s idf=%s built=%s_%s\n", app->project_name, app->version,
           esp_get_idf_version(), app->date, app->time);

#if CONFIG_APP_MODE_CAPTURE
    run_capture_mode();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#else
    bool ok = run_selftest();

    printf("@@HEAP internal_free=%u psram_free=%u min_internal=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    if (ok) {
        printf("@@RESULT PASS\n");
    }
    fflush(stdout);

    /* Application work goes here (streaming, recording loop, ...). */
    ESP_LOGI(TAG, "self-test done, idling");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#endif
}
