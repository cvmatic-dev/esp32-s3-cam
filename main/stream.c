#include "stream.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "sdkconfig.h"

#include "capture.h"

static const char *TAG = "stream";
static httpd_handle_t s_server;

static esp_err_t root_get(httpd_req_t *req)
{
    static const char html[] =
        "<!doctype html><html><head>"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>esp32s3-cam</title></head>"
        "<body style=\"margin:0;background:#000;text-align:center\">"
        "<img src=\"/stream\" style=\"width:100%;height:auto\">"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t stream_get(httpd_req_t *req)
{
    if (httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame") != ESP_OK) {
        return ESP_FAIL;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    size_t cap = app_capture_jpeg_capacity();
    if (cap == 0) {
        cap = 256 * 1024;
    }
    uint8_t *jpeg = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (!jpeg) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_FAIL;
    }

    const TickType_t period = pdMS_TO_TICKS(1000 / CONFIG_APP_CAPTURE_STREAM_FPS);
    uint32_t last_seq = 0;
    while (true) {
        size_t len = 0;
        uint32_t seq = 0;
        if (app_capture_latest(jpeg, cap, &len, &seq) && seq != last_seq) {
            last_seq = seq;
            char hdr[96];
            int n = snprintf(hdr, sizeof(hdr),
                             "\r\n--frame\r\nContent-Type: image/jpeg\r\n"
                             "Content-Length: %u\r\n\r\n", (unsigned)len);
            if (httpd_resp_send_chunk(req, hdr, n) != ESP_OK) {
                break;
            }
            if (httpd_resp_send_chunk(req, (const char *)jpeg, len) != ESP_OK) {
                break;
            }
        }
        vTaskDelay(period);
    }
    heap_caps_free(jpeg);
    ESP_LOGI(TAG, "stream client disconnected");
    return ESP_OK;
}

esp_err_t app_stream_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.max_uri_handlers = 4;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.max_open_sockets = 4;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = NULL };
    httpd_uri_t stream = { .uri = "/stream", .method = HTTP_GET, .handler = stream_get, .user_ctx = NULL };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &stream);

    ESP_LOGI(TAG, "http server on :80  /  /stream @ %d fps", CONFIG_APP_CAPTURE_STREAM_FPS);
    return ESP_OK;
}
