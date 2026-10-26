idf_component_register(SRCS "wifi_health_detector_main.c" "wifi.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_lcd esp_wifi esp_netif esp_event nvs_flash qrcode wifi_provisioning)