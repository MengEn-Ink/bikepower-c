idf_component_register(
    SRCS "app_main.c"
         "ble_bikepower.c"
         "app_state.c"
         "button.c"
         "led.c"
         "board_detect.c"
         "nvs_store.c"
         "radio_ctrl.c"
         "wifi_portal.c"
         "captive_portal.c"
         "ota_update.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt esp_timer esp_driver_gpio esp_system
             esp_wifi esp_event esp_netif esp_http_server bikepower_domain
             esp_driver_rmt app_update
)

option(OTA_TEST_AUTO_CONFIG "测试用：启动自动触发配网（正式发版保持 OFF）" OFF)
if(OTA_TEST_AUTO_CONFIG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE OTA_TEST_AUTO_CONFIG)
    message(STATUS "OTA_TEST_AUTO_CONFIG=ON（测试固件，启动自动配网）")
endif()
