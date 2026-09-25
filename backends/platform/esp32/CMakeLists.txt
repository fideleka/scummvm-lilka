# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Target: LilyGo T-Deck v1 (ESP32-S3).
# No BSP component is used — T-Deck peripherals are driven directly with
# ESP-IDF's esp_lcd, i2s_std, i2c, gpio and sdspi_host drivers.

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(scummvm)
