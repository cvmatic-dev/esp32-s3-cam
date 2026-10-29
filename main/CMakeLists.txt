idf_component_register(
    SRCS
        "main.c"
        "camera.c"
        "sdcard.c"
        "frame_dump.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_driver_sdmmc
        sdmmc
        fatfs
        mbedtls
        esp_timer
        esp_psram
        esp_rom
        esp_app_format
)
