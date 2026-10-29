#include "capture.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_camera.h"
#include "sdkconfig.h"

#include "board_pins.h"
#include "sdcard.h"

static const char *TAG = "capture";

/*
 * The ESP32-S3 has no hardware JPEG encoder (only ROM JPEG decode), so frames
 * are encoded by the *sensor* (OV5640) and forced to grayscale in its ISP via
 * the "Grayscale" special effect (U=V=0x80). The result is a Y-only JPEG that
 * can be written to SD and streamed to a browser unchanged.
 */

typedef struct {
    framesize_t fs;
    const char *name;
    int w, h;
} fs_entry_t;

static const fs_entry_t k_sizes[] = {
    { FRAMESIZE_HD,   "HD",   1280, 720 },
    { FRAMESIZE_SVGA, "SVGA", 800,  600 },
    { FRAMESIZE_VGA,  "VGA",  640,  480 },
    { FRAMESIZE_QVGA, "QVGA", 320,  240 },
};
#define NUM_SIZES ((int)(sizeof(k_sizes) / sizeof(k_sizes[0])))

#if defined(CONFIG_APP_CAPTURE_FRAMESIZE_SVGA)
#define START_INDEX 1
#elif defined(CONFIG_APP_CAPTURE_FRAMESIZE_VGA)
#define START_INDEX 2
#elif defined(CONFIG_APP_CAPTURE_FRAMESIZE_QVGA)
#define START_INDEX 3
#else
#define START_INDEX 0
#endif

static int s_size_idx = START_INDEX;
static int s_trow_ns;
static int s_exp_lines;
static size_t s_cap;

static SemaphoreHandle_t s_latest_lock;
static uint8_t *s_latest;
static size_t s_latest_len;
static volatile uint32_t s_latest_seq;

static volatile int s_sd_drops;
static volatile int s_fps_x10;
static volatile int s_achieved_fps_x10;
static volatile int s_copy_us;
static FILE *s_sd_file;
static int s_sd_saved;
static char s_run_dir[64];

/* ------------------------------------------------------------------ sensor */

static void configure_manual(sensor_t *s)
{
    /* AEC / AGC off -> both driven manually from 0x3503. */
    s->set_gain_ctrl(s, 0);
    s->set_exposure_ctrl(s, 0);
    s->set_aec2(s, 0);
    s->set_agc_gain(s, 0);   /* minimum (1x) fixed gain */
    /* AWB off. */
    s->set_whitebal(s, 0);
    s->set_awb_gain(s, 0);
#if CONFIG_APP_CAPTURE_DISABLE_DENOISE
    s->set_denoise(s, 0);
#endif
    /* 50/60 Hz anti-flicker (banding) filter off, and lift the auto-exposure
     * ceiling registers so the manual exposure is not capped. */
    s->set_reg(s, 0x3c00, 0xFF, 0x00);
    s->set_reg(s, 0x3c01, 0xFF, 0x00);
    s->set_reg(s, 0x3a02, 0xFFFF, 0x0FFF);
    s->set_reg(s, 0x3a14, 0xFFFF, 0x0FFF);
}

/* Count frames over a window and read back the frame length (VTS). */
static bool measure(sensor_t *s, int ms, float *fps_out, int *vts_out)
{
    int vts = s->get_reg(s, 0x380e, 0xFFFF);
    if (vts <= 0) {
        return false;
    }
    int64_t t0 = esp_timer_get_time();
    int n = 0;
    while ((esp_timer_get_time() - t0) < (int64_t)ms * 1000) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            n++;
            esp_camera_fb_return(fb);
        }
    }
    int64_t dt = esp_timer_get_time() - t0;
    if (dt <= 0 || n < 2) {
        return false;
    }
    *fps_out = (float)n * 1e6f / (float)dt;
    *vts_out = vts;
    return true;
}

/* Measure the native frame rate at the driver's default frame timing.
 * Shortening VTS to chase the target reliably overruns the DVP/PSRAM path
 * (FB-OVF), so we run at the sensor's native rate and just report it.
 * tROW = 1/(fps*VTS) is used to convert the requested exposure time. */
static float calibrate(sensor_t *s, int idx)
{
    float fps = 0;
    int vts = 0;
    if (!measure(s, 600, &fps, &vts)) {
        return 0;
    }
    s_trow_ns = (int)(1e9f / (fps * (float)vts) + 0.5f);
    ESP_LOGI(TAG, "%s: %.1f fps (VTS %d), tROW %d ns", k_sizes[idx].name, fps, vts, s_trow_ns);
    return fps;
}

static void set_manual_exposure(sensor_t *s, int num, int den)
{
    if (s_trow_ns <= 0) {
        s_trow_ns = 1;
    }
    float exposure_s = (float)num / (float)den;
    int lines = (int)(exposure_s * 1e9f / (float)s_trow_ns + 0.5f);
    int vts = s->get_reg(s, 0x380e, 0xFFFF);
    if (lines > vts) {
        lines = vts;
    }
    s->set_aec_value(s, lines);
    s_exp_lines = lines;
}

/* ------------------------------------------------------------------- tasks */

static void capture_task(void *arg)
{
    (void)arg;
    int secs = 0;
    int64_t win_start = esp_timer_get_time();
    int win_frames = 0;

    while (true) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            vTaskDelay(1);
            continue;
        }
        if (fb->len > s_cap) {
            s_sd_drops++;                       /* unexpectedly large frame */
            esp_camera_fb_return(fb);
            continue;
        }

        /* Publish to the stream. */
        if (xSemaphoreTake(s_latest_lock, portMAX_DELAY) == pdTRUE) {
            memcpy(s_latest, fb->buf, fb->len);
            s_latest_len = fb->len;
            s_latest_seq++;
            xSemaphoreGive(s_latest_lock);
        }

#if CONFIG_APP_CAPTURE_SAVE_SD
        /* Append straight from the camera buffer (valid until fb_return). One
         * sequential .mjpeg file: per-frame files cost ~60 ms each on FAT32,
         * which caps SD at ~15 fps. */
        if (s_sd_file) {
            int64_t c0 = esp_timer_get_time();
            if (fwrite(fb->buf, 1, fb->len, s_sd_file) == fb->len) {
                if (++s_sd_saved % 25 == 0) {
                    fflush(s_sd_file);
                }
            } else {
                s_sd_drops++;
                fclose(s_sd_file);
                s_sd_file = NULL;
            }
            s_copy_us = (int)(esp_timer_get_time() - c0);
        }
#endif
        esp_camera_fb_return(fb);

        win_frames++;
        int64_t now = esp_timer_get_time();
        if (now - win_start >= 1000000) {
            s_fps_x10 = (int)(win_frames * 10000000LL / (now - win_start));
            win_frames = 0;
            win_start = now;
            if (++secs % 5 == 0) {
                printf("@@STATS capture_fps_x10=%d sd_saved=%d sd_drops=%d jpeg_kb=%u sd_ms=%d\n",
                       s_fps_x10, s_sd_saved, s_sd_drops, (unsigned)(s_latest_len / 1024),
                       s_copy_us / 1000);
                fflush(stdout);
            }
        }
    }
}

/* --------------------------------------------------------------- lifecycle */

static esp_err_t camera_init(framesize_t fs)
{
    const camera_config_t cfg = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,
        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,
        .xclk_freq_hz = CAM_XCLK_HZ,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size = fs,
        .jpeg_quality = CONFIG_APP_CAPTURE_JPEG_QUALITY,   /* sensor scale: lower = better */
        .fb_count = 3,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };
    return esp_camera_init(&cfg);
}

static void make_run_dir(void)
{
    mkdir(SD_MOUNT_POINT "/cap", 0775);
    for (int i = 0; i < 1000; i++) {
        struct stat st;
        snprintf(s_run_dir, sizeof(s_run_dir), SD_MOUNT_POINT "/cap/run_%03d", i);
        if (stat(s_run_dir, &st) != 0) {
            mkdir(s_run_dir, 0775);
            return;
        }
    }
    snprintf(s_run_dir, sizeof(s_run_dir), SD_MOUNT_POINT "/cap/run_999");
}

esp_err_t app_capture_start(void)
{
    sensor_t *s = NULL;
    float achieved = 0;

    for (int i = START_INDEX; i < NUM_SIZES; i++) {
        if (camera_init(k_sizes[i].fs) != ESP_OK) {
            ESP_LOGE(TAG, "camera init failed at %s", k_sizes[i].name);
            return ESP_FAIL;
        }
        s = esp_camera_sensor_get();
        if (s == NULL) {
            esp_camera_deinit();
            return ESP_FAIL;
        }
        configure_manual(s);
        s->set_pixformat(s, PIXFORMAT_JPEG);
        s->set_framesize(s, k_sizes[i].fs);
        s->set_special_effect(s, 2);   /* 2 = Grayscale (U=V=0x80) */

        achieved = calibrate(s, i);
        s_size_idx = i;

        bool good = achieved >= 0.9f * (float)CONFIG_APP_CAPTURE_FPS_TARGET;
        if (good || !CONFIG_APP_CAPTURE_AUTOSHRINK || i == NUM_SIZES - 1) {
            if (!good) {
                ESP_LOGW(TAG, "%s only reaches %.1f fps (target %d)",
                         k_sizes[i].name, achieved, CONFIG_APP_CAPTURE_FPS_TARGET);
            }
            break;
        }
        esp_camera_deinit();
        ESP_LOGW(TAG, "%.1f fps < target %d; shrinking framesize",
                 achieved, CONFIG_APP_CAPTURE_FPS_TARGET);
    }
    s_achieved_fps_x10 = (int)(achieved * 10.0f + 0.5f);

    set_manual_exposure(s, CONFIG_APP_CAPTURE_EXPOSURE_NUM, CONFIG_APP_CAPTURE_EXPOSURE_DEN);

    const fs_entry_t *fs = &k_sizes[s_size_idx];
    s_cap = (size_t)fs->w * (size_t)fs->h / 3;

    s_latest = heap_caps_malloc(s_cap, MALLOC_CAP_SPIRAM);
    s_latest_lock = xSemaphoreCreateMutex();
    if (!s_latest || !s_latest_lock) {
        return ESP_ERR_NO_MEM;
    }

    if (app_sd_mounted()) {
        make_run_dir();
#if CONFIG_APP_CAPTURE_SAVE_SD
        char path[96];
        snprintf(path, sizeof(path), "%s/video.mjpeg", s_run_dir);
        s_sd_file = fopen(path, "wb");
        ESP_LOGI(TAG, "recording to %s", path);
#endif
    }

    xTaskCreatePinnedToCore(capture_task, "capture", 4096, NULL, 6, NULL, 1);

    /* Let the pipeline settle so the reported fps is real. */
    vTaskDelay(pdMS_TO_TICKS(2500));

    printf("@@CHECK camera=OK framesize=%s pixfmt=JPEG+grey fps_x10=%d pid=0x%04x\n",
           fs->name, s_achieved_fps_x10, s->id.PID);
    printf("@@CHECK exposure=OK num=%d den=%d lines=%d trow_ns=%d aec=%d agc=%d awb=%d\n",
           CONFIG_APP_CAPTURE_EXPOSURE_NUM, CONFIG_APP_CAPTURE_EXPOSURE_DEN,
           s_exp_lines, s_trow_ns, s->status.aec, s->status.agc, s->status.awb);
    printf("@@STATS capture_fps_x10=%d copy_us=%d sd_drops=%d jpeg_kb=%u\n",
           s_fps_x10, s_copy_us, s_sd_drops, (unsigned)(s_latest_len / 1024));
    return ESP_OK;
}

bool app_capture_latest(uint8_t *dst, size_t cap, size_t *len, uint32_t *seq)
{
    if (!s_latest_lock || s_latest_len == 0 || cap < s_latest_len) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(s_latest_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(dst, s_latest, s_latest_len);
        *len = s_latest_len;
        *seq = s_latest_seq;
        ok = true;
        xSemaphoreGive(s_latest_lock);
    }
    return ok;
}

size_t app_capture_jpeg_capacity(void)
{
    return s_cap;
}
