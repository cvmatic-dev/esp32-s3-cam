#include "wifi_ap.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_ap";
static char s_ip[16] = "0.0.0.0";

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base != WIFI_EVENT) {
        return;
    }
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "station " MACSTR " joined, aid=%d", MAC2STR(e->mac), e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "station " MACSTR " left, aid=%d", MAC2STR(e->mac), e->aid);
    }
}

esp_err_t app_wifi_ap_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        return err;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        on_wifi_event, NULL, NULL));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, CONFIG_APP_WIFI_SSID, sizeof(wc.ap.ssid));
    wc.ap.ssid_len = strlen(CONFIG_APP_WIFI_SSID);
    wc.ap.channel = CONFIG_APP_WIFI_CHANNEL;
    wc.ap.max_connection = CONFIG_APP_WIFI_MAX_CONN;
    if (strlen(CONFIG_APP_WIFI_PASSWORD) >= 8) {
        strlcpy((char *)wc.ap.password, CONFIG_APP_WIFI_PASSWORD, sizeof(wc.ap.password));
        wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip;
    if (ap && esp_netif_get_ip_info(ap, &ip) == ESP_OK) {
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip.ip));
    }
    ESP_LOGI(TAG, "SoftAP '%s' up (%s), ip=%s", CONFIG_APP_WIFI_SSID,
             wc.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2", s_ip);
    return ESP_OK;
}

const char *app_wifi_ap_ip(void)
{
    return s_ip;
}
