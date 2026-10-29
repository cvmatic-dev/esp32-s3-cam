#pragma once
#include "esp_err.h"

/* Bring up a WPA2 SoftAP (SSID/password/channel from Kconfig). */
esp_err_t app_wifi_ap_start(void);

/* AP IPv4 address as a string ("0.0.0.0" until started). */
const char *app_wifi_ap_ip(void);
