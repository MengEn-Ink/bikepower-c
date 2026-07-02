idf_component_register(
    SRCS "app_main.c"
         "ble_bikepower.c"
         "ble_scanner.c"
         "app_state.c"
         "button.c"
         "led.c"
         "board_detect.c"
         "nvs_store.c"
         "device_template_store.c"
         "radio_ctrl.c"
         "wifi_portal.c"
         "captive_portal.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash bt esp_timer esp_driver_gpio esp_system
             esp_wifi esp_event esp_netif esp_http_server bikepower_domain
             esp_driver_rmt
)

# ===== web 静态资源内联（v3.2.0 前后端分离） =====
# 把 main/web/{index.html,app.css,app.js} 合并为单一内联 HTML，输出 web_index_html.h
# 由 scripts/inline_static.py 在编译时生成，wifi_portal.c 通过 #include 引用。
# iOS Captive Portal WebView 不能加载外部 .css/.js，必须内联。
# 注意：add_custom_command 必须放在 idf_component_register 之后，
#       否则 ESP-IDF 在 component_get_requirements 阶段会报 "command is not scriptable"。
set(GENERATED_WEB_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${GENERATED_WEB_DIR})
set(GENERATED_WEB_HEADER "${GENERATED_WEB_DIR}/web_index_html.h")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT ${GENERATED_WEB_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/inline_static.py
            --web-dir ${CMAKE_CURRENT_LIST_DIR}/web
            --output ${GENERATED_WEB_HEADER}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/web/index.html
            ${CMAKE_CURRENT_LIST_DIR}/web/app.css
            ${CMAKE_CURRENT_LIST_DIR}/web/app.js
            ${CMAKE_SOURCE_DIR}/scripts/inline_static.py
    COMMENT "Inlining web static resources -> web_index_html.h"
    VERBATIM
)
add_custom_target(web_index_html DEPENDS ${GENERATED_WEB_HEADER})

# 把生成目录加入 include 路径，wifi_portal.c 才能 #include "web_index_html.h"
target_include_directories(${COMPONENT_LIB} PRIVATE ${GENERATED_WEB_DIR})
# 让 COMPONENT_LIB 依赖生成步骤（保证编译前先生成头文件）
add_dependencies(${COMPONENT_LIB} web_index_html)

option(PORTAL_TEST_AUTO_CONFIG "测试用：启动自动触发配网（正式发版保持 OFF）" OFF)
if(PORTAL_TEST_AUTO_CONFIG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE PORTAL_TEST_AUTO_CONFIG)
    message(STATUS "PORTAL_TEST_AUTO_CONFIG=ON（测试固件，启动自动配网）")
endif()
