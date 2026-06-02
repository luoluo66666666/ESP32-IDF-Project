idf_component_register(
    SRCS "Http_ota.c" "ota_rollback.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp_https_ota
        app_update
        freertos
        esp_system
        nvs_flash
)
