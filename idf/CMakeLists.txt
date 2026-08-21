# Shot Stopper firmware built with ESP-IDF (Arduino-ESP32 as a component).
# Official supported builds: ./scripts/build-idf → build-idf/<arch>.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Board-parity defines (Arduino FQBN: PSRAM + USB CDC on boot / Hardware CDC).
# Product options (remote CN9, buzzer) live in main/Kconfig.projbuild and
# sdkconfig.defaults; SHOT_STOPPER_EXTRA_FLAGS may still pass -D overrides.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "BOARD_HAS_PSRAM" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_CDC_ON_BOOT=1" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_USB_MODE=1" APPEND)

if(DEFINED ENV{SHOT_STOPPER_EXTRA_FLAGS} AND NOT "$ENV{SHOT_STOPPER_EXTRA_FLAGS}" STREQUAL "")
  separate_arguments(_ss_extra NATIVE_COMMAND "$ENV{SHOT_STOPPER_EXTRA_FLAGS}")
  foreach(_opt IN LISTS _ss_extra)
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

project(shotstopper)
