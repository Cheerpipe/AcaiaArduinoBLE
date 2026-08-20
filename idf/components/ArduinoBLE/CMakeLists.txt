set(ARDUINO_BLE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ArduinoBLE")
if(DEFINED ENV{ARDUINO_BLE_PATH} AND NOT "$ENV{ARDUINO_BLE_PATH}" STREQUAL "")
  set(ARDUINO_BLE_PATH "$ENV{ARDUINO_BLE_PATH}")
endif()

if(NOT EXISTS "${ARDUINO_BLE_PATH}/src/ArduinoBLE.h")
  message(FATAL_ERROR
    "ArduinoBLE 2.1.0 not found at ${ARDUINO_BLE_PATH}. "
    "./scripts/build-idf clones it into idf/third_party/ArduinoBLE.")
endif()

file(GLOB_RECURSE ARDUINO_BLE_SRCS "${ARDUINO_BLE_PATH}/src/*.cpp")
list(FILTER ARDUINO_BLE_SRCS EXCLUDE REGEX ".*/extras/.*")
# Only the ESP32 VHCI transport belongs in this firmware. The other HCI
# backends are #ifdef'd out, but they still define HCITransport if compiled.
list(FILTER ARDUINO_BLE_SRCS EXCLUDE REGEX
  "HCI(Cordio|Uart|NinaSpi|Silabs|VirtualTransport(AT|RPC|Zephyr))")

idf_component_register(
  SRCS ${ARDUINO_BLE_SRCS}
  INCLUDE_DIRS
    "${ARDUINO_BLE_PATH}/src"
    "${ARDUINO_BLE_PATH}/src/local"
    "${ARDUINO_BLE_PATH}/src/remote"
    "${ARDUINO_BLE_PATH}/src/utility"
  REQUIRES arduino-esp32 bt nvs_flash
)
