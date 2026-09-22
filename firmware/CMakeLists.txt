# Top-level project file for the ETeams smart-room controller firmware.
#
# Target: ESP32-S3, 8 MB flash, NO PSRAM (GPIO36 is loaded by the 5 V
# regulator EN net on this mainboard — enabling octal PSRAM boot-loops the
# stage-2 bootloader with RTCWDT_RTC_RST. See doc/psram.md in the repo).

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ste_fw_c)
