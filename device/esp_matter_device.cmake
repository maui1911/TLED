cmake_minimum_required(VERSION 3.5)
if (NOT ("${IDF_TARGET}" STREQUAL "esp32c6" ))
    message(FATAL_ERROR "please set esp32c6 as the IDF_TARGET using 'idf.py --preview set-target esp32c6'")
endif()

SET(device_type     esp32c6_devkit_c)
SET(led_type        none)
SET(button_type     iot)

# Only include button driver - TLED has its own LED driver in app_driver.cpp
# This avoids the led_strip ^1.0.0 dependency from esp-matter's led_driver
SET(extra_components_dirs_append "$ENV{ESP_MATTER_PATH}/device_hal/button_driver/iot_button")
