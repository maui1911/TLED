cmake_minimum_required(VERSION 3.5)

# Supported targets: ESP32-C6 (reference board) and ESP32-H2.
# Both are Thread-capable (IEEE 802.15.4) with BLE for Matter commissioning.
if ("${IDF_TARGET}" STREQUAL "esp32c6")
    SET(device_type     esp32c6_devkit_c)
elseif ("${IDF_TARGET}" STREQUAL "esp32h2")
    # TLED does not use the esp-matter device HAL for LEDs or buttons (it has its
    # own drivers), but esp-matter's build still needs a device_type that exists
    # in the esp-matter checkout. Use the first H2 device directory that is present.
    SET(device_type "")
    foreach(_candidate esp32h2_devkit_c esp32h2_devkit_m)
        if (IS_DIRECTORY "$ENV{ESP_MATTER_PATH}/device_hal/device/${_candidate}")
            SET(device_type ${_candidate})
            break()
        endif()
    endforeach()
    if ("${device_type}" STREQUAL "")
        message(FATAL_ERROR "No ESP32-H2 device directory found in "
                "$ENV{ESP_MATTER_PATH}/device_hal/device. "
                "List that directory and set device_type in device/esp_matter_device.cmake "
                "to the H2 entry that exists in your esp-matter checkout.")
    endif()
else()
    message(FATAL_ERROR "Unsupported IDF_TARGET '${IDF_TARGET}'. TLED supports esp32c6 and esp32h2: "
            "run 'idf.py set-target esp32c6' or 'idf.py set-target esp32h2'.")
endif()

SET(led_type        none)
SET(button_type     iot)

# Only include button driver - TLED has its own LED driver in app_driver.cpp
# This avoids the led_strip ^1.0.0 dependency from esp-matter's led_driver
SET(extra_components_dirs_append "$ENV{ESP_MATTER_PATH}/device_hal/button_driver/iot_button")
