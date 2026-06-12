idf_component_register(
    SRCS "app_main.c"
         "ble_bikepower.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt esp_timer bikepower_domain
)
