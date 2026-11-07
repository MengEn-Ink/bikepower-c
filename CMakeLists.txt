# BikePower C / ESP-IDF + NimBLE
# 目标基线：ESP-IDF v5.5.4（本地工具链），目标芯片 ESP32-C3
cmake_minimum_required(VERSION 3.16)

# 强制 PROJECT_VER 从 bikepower_config.h 提取，确保 esp_app_desc.version
# 与 FIRMWARE_VERSION 宏一致，避免 OTA 防降级误判（git describe 含后缀）。
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/main/bikepower_config.h" _fv_line REGEX "^[ \t]*#define[ \t]+FIRMWARE_VERSION")
if(_fv_line)
    string(REGEX REPLACE "^[^\"]*\"([^\"]+)\".*" "\\1" PROJECT_VER "${_fv_line}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bikepower-c)
