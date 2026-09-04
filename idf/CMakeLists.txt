# Shot Stopper firmware built with ESP-IDF (Arduino-ESP32 as a component).
# Official supported builds: ./scripts/build-idf → build-idf/<arch>.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Board-parity defines (Arduino FQBN: PSRAM + Hardware CDC). CDC stays off
# until setup() sees the USB console jumper (GPIO4–GND) and calls Serial.begin
# on HWCDC (ShotStopperUsbConsole.h), unless SHOT_STOPPER_EXTRA_FLAGS includes
# -DSHOT_STOPPER_ENABLE_JTAG=1 (ARDUINO_USB_CDC_ON_BOOT=1 + IDF console).
# Product options (remote machine control, buzzer, machine type, JTAG) live in
# main/Kconfig.projbuild and sdkconfig.defaults; SHOT_STOPPER_EXTRA_FLAGS may
# still pass -D overrides. idf.py does not reconfigure when only an env var
# changes, so ./scripts/build-idf writes CMAKE_BINARY_DIR/shot_stopper_extra_flags
# and drops CMakeCache.txt when that stamp changes.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "BOARD_HAS_PSRAM" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_MODE=1" APPEND)

set(_ss_extra_flags_file "${CMAKE_BINARY_DIR}/shot_stopper_extra_flags")
set(_ss_extra_raw "")
set(_ss_cdc_on_boot 0)
if(DEFINED ENV{SHOT_STOPPER_EXTRA_FLAGS})
  set(_ss_extra_raw "$ENV{SHOT_STOPPER_EXTRA_FLAGS}")
endif()
if(_ss_extra_raw STREQUAL "" AND EXISTS "${_ss_extra_flags_file}")
  file(READ "${_ss_extra_flags_file}" _ss_extra_raw)
  string(STRIP "${_ss_extra_raw}" _ss_extra_raw)
endif()

if(NOT _ss_extra_raw STREQUAL "")
  message(STATUS "SHOT_STOPPER_EXTRA_FLAGS=${_ss_extra_raw}")
  separate_arguments(_ss_extra NATIVE_COMMAND "${_ss_extra_raw}")
  foreach(_opt IN LISTS _ss_extra)
    if(_opt STREQUAL "-DSHOT_STOPPER_ENABLE_JTAG=1")
      set(_ss_cdc_on_boot 1)
    endif()
    if(_opt MATCHES "^-D(.+)$")
      idf_build_set_property(COMPILE_DEFINITIONS "${CMAKE_MATCH_1}" APPEND)
    elseif(_opt MATCHES "^-W")
      # gcc C rejects C++-only warnings such as -Werror=deprecated-copy (the
      # default extra flag). Keep -W extras on C++ so mbedtls/.c still build.
      idf_build_set_property(CXX_COMPILE_OPTIONS "${_opt}" APPEND)
    else()
      idf_build_set_property(COMPILE_OPTIONS "${_opt}" APPEND)
    endif()
  endforeach()
endif()

idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=${_ss_cdc_on_boot}" APPEND)
if(_ss_cdc_on_boot)
  message(STATUS "USB Serial/JTAG enabled (ARDUINO_USB_CDC_ON_BOOT=1)")
endif()

project(shotstopper)

# ninja cmake_check reconfigures when ./scripts/build-idf updates the stamp.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ss_extra_flags_file}")

# Arduino-ESP32 compiles Esp.cpp as C++ against IDF SPI flash HAL headers
# that use partial designated initializers (gpspi_flash_ll.h /
# spimem_flash_ll.h). That is Espressif noise, not firmware.
idf_component_get_property(_ss_arduino_lib espressif__arduino-esp32 COMPONENT_LIB)
target_compile_options(${_ss_arduino_lib} PRIVATE -Wno-missing-field-initializers)

# esp-modbus (pulled by arduino-esp32) forces -Werror. Under -O2, GCC raises
# -Wstringop-truncation on its own strncpy in port_tcp_utils.c (bound ==
# strlen(src), so no NUL is copied). Not our code; silence rather than patch
# managed_components (overwritten on component update).
idf_component_get_property(_ss_modbus_lib espressif__esp-modbus COMPONENT_LIB)
target_compile_options(${_ss_modbus_lib} PRIVATE -Wno-stringop-truncation)

# ESP-IDF 5.5.5 NimBLE assigns key_rc in ble_hs_startup_go() even when
# BLE_HS_PVCY is disabled, leaving it unused in that upstream configuration.
# Keep the suppression scoped to ESP-IDF's bt component; firmware warnings
# remain visible and the SDK itself is never patched.
idf_component_get_property(_ss_bt_lib bt COMPONENT_LIB)
target_compile_options(${_ss_bt_lib} PRIVATE -Wno-unused-but-set-variable)
