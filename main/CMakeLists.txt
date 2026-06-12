idf_component_register(
    SRCS "app_main.c"
         "ble_bikepower.c"
         "app_state.c"
         "button.c"
         "led.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt esp_timer esp_driver_gpio esp_system bikepower_domain
)
