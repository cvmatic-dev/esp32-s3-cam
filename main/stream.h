#pragma once
#include "esp_err.h"

/* Start the HTTP server: "/" (viewer page) and "/stream" (MJPEG). */
esp_err_t app_stream_start(void);
