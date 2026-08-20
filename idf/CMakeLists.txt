# Shot Stopper firmware built with ESP-IDF (Arduino-ESP32 as a component).
# Production images still come from ./scripts/build (arduino-cli).
cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Arduino FQBN parity: PSRAM + USB CDC on boot with Hardware CDC/JTAG
# (CDCOnBoot=cdc, default USBMode=hwcdc).
idf_build_set_property(COMPILE_OPTIONS "-DBOARD_HAS_PSRAM" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_CDC_ON_BOOT=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DARDUINO_USB_MODE=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DSHOT_STOPPER_ENABLE_REMOTE_CN9=1" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-DSHOT_STOPPER_ENABLE_BUZZER=2" APPEND)

if(DEFINED ENV{SHOT_STOPPER_EXTRA_FLAGS} AND NOT "$ENV{SHOT_STOPPER_EXTRA_FLAGS}" STREQUAL "")
  separate_arguments(_ss_extra NATIVE_COMMAND "$ENV{SHOT_STOPPER_EXTRA_FLAGS}")
  foreach(_opt IN LISTS _ss_extra)
    idf_build_set_property(COMPILE_OPTIONS "${_opt}" APPEND)
  endforeach()
endif()

project(shotstopper)
