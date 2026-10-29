#include "camera.h"
#include "board_pins.h"
#include "sdkconfig.h"
#include "esp_log.h"

static const char *TAG = "camera";

#if defined(CONFIG_APP_FRAMESIZE_QVGA)
#define APP_FRAMESIZE FRAMESIZE_QVGA
#elif defined(CONFIG_APP_FRAMESIZE_VGA)
#define APP_FRAMESIZE FRAMESIZE_VGA
#elif defined(CONFIG_APP_FRAMESIZE_HD)
#define APP_FRAMESIZE FRAMESIZE_HD
#elif defined(CONFIG_APP_FRAMESIZE_UXGA)
#define APP_FRAMESIZE FRAMESIZE_UXGA
#elif defined(CONFIG_APP_FRAMESIZE_QSXGA)
#define APP_FRAMESIZE FRAMESIZE_QSXGA
#else
#define APP_FRAMESIZE FRAMESIZE_SVGA
#endif

#ifdef CONFIG_APP_WB_MODE_SUNNY
#define APP_WB_MODE 1
#elif defined(CONFIG_APP_WB_MODE_CLOUDY)
#define APP_WB_MODE 2
#elif defined(CONFIG_APP_WB_MODE_OFFICE)
#define APP_WB_MODE 3
#elif defined(CONFIG_APP_WB_MODE_HOME)
#define APP_WB_MODE 4
#else
#define APP_WB_MODE 0 /* auto */
#endif

esp_err_t app_camera_init(void)
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
        .frame_size = APP_FRAMESIZE,
        .jpeg_quality = CONFIG_APP_JPEG_QUALITY,
        .fb_count = 2,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST,
    };

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        return ESP_FAIL;
    }
    if (s->id.PID != OV5640_PID) {
        ESP_LOGE(TAG, "unexpected sensor PID 0x%04x (want 0x%04x)", s->id.PID, OV5640_PID);
        return ESP_ERR_NOT_FOUND;
    }

    /* The default register table already enables these; re-asserting makes the
     * intent explicit and lets APP_WB_MODE pin a fixed white balance (mode != 0
     * switches 0x3406 to manual AWB gain) when auto drifts to a colour cast. */
    s->set_gain_ctrl(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_awb_gain(s, 1);
    if (s->set_wb_mode(s, APP_WB_MODE) != 0) {
        ESP_LOGW(TAG, "set_wb_mode(%d) failed", APP_WB_MODE);
    }

    ESP_LOGI(TAG, "OV5640 ready, framesize=%d quality=%d wb_mode=%d",
             (int)APP_FRAMESIZE, CONFIG_APP_JPEG_QUALITY, APP_WB_MODE);
    return ESP_OK;
}

void app_camera_warmup(int n)
{
    for (int i = 0; i < n; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            esp_camera_fb_return(fb);
        }
    }
}

uint16_t app_camera_pid(void)
{
    sensor_t *s = esp_camera_sensor_get();
    return s ? s->id.PID : 0;
}
