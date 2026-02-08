# TLED - Matter-over-Thread LED Controller
# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif(NOT DEFINED ENV{ESP_MATTER_PATH})

if(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})
    # Use our custom device cmake that excludes esp-matter's led_driver
    # (TLED has its own LED driver with SK6812 RGBW support)
    set(ENV{ESP_MATTER_DEVICE_PATH} ${CMAKE_SOURCE_DIR}/device)
endif(NOT DEFINED ENV{ESP_MATTER_DEVICE_PATH})

set(PROJECT_VER "0.7.0")
set(PROJECT_VER_NUMBER 9)

set(ESP_MATTER_PATH $ENV{ESP_MATTER_PATH})
set(MATTER_SDK_PATH ${ESP_MATTER_PATH}/connectedhomeip/connectedhomeip)

# This should be done before using the IDF_TARGET variable.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include(${ESP_MATTER_PATH}/examples/common/cmake_common/components_include.cmake)
include($ENV{ESP_MATTER_DEVICE_PATH}/esp_matter_device.cmake)

set(EXTRA_COMPONENT_DIRS
    "${ESP_MATTER_PATH}/examples/common"
    "${MATTER_SDK_PATH}/config/esp32/components"
    "${ESP_MATTER_PATH}/components"
    ${extra_components_dirs_append})

project(tled)

idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++17;-Os;-DCHIP_HAVE_CONFIG_H;-Wno-overloaded-virtual" APPEND)
idf_build_set_property(C_COMPILE_OPTIONS "-Os" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security" APPEND)
