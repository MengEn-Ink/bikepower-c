idf_component_register(
    SRCS "app_main.c"
         "ble_bikepower.c"
         "app_state.c"
         "button.c"
         "led.c"
         "nvs_store.c"
         "radio_ctrl.c"
         "wifi_portal.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt esp_timer esp_driver_gpio esp_system
             esp_wifi esp_event esp_netif esp_http_server bikepower_domain
)
