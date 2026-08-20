set(ARDUINO_BLE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../third_party/ArduinoBLE")
if(DEFINED ENV{ARDUINO_BLE_PATH} AND NOT "$ENV{ARDUINO_BLE_PATH}" STREQUAL "")
  set(ARDUINO_BLE_PATH "$ENV{ARDUINO_BLE_PATH}")
endif()

if(NOT EXISTS "${ARDUINO_BLE_PATH}/src/ArduinoBLE.h")
  message(FATAL_ERROR
    "ArduinoBLE 2.1.0 not found at ${ARDUINO_BLE_PATH}. "
    "./scripts/build-idf clones it into idf/third_party/ArduinoBLE.")
endif()

# Explicit SRCS (ESP32 VHCI transport only). Avoid GLOB so CMake reconfigure
# is not required when unrelated transports appear upstream.
set(ARDUINO_BLE_SRCS
  "${ARDUINO_BLE_PATH}/src/BLEAdvertisingData.cpp"
  "${ARDUINO_BLE_PATH}/src/BLECharacteristic.cpp"
  "${ARDUINO_BLE_PATH}/src/BLEDescriptor.cpp"
  "${ARDUINO_BLE_PATH}/src/BLEDevice.cpp"
  "${ARDUINO_BLE_PATH}/src/BLEService.cpp"
  "${ARDUINO_BLE_PATH}/src/BLEStringCharacteristic.cpp"
  "${ARDUINO_BLE_PATH}/src/BLETypedCharacteristics.cpp"
  "${ARDUINO_BLE_PATH}/src/local/BLELocalAttribute.cpp"
  "${ARDUINO_BLE_PATH}/src/local/BLELocalCharacteristic.cpp"
  "${ARDUINO_BLE_PATH}/src/local/BLELocalDescriptor.cpp"
  "${ARDUINO_BLE_PATH}/src/local/BLELocalDevice.cpp"
  "${ARDUINO_BLE_PATH}/src/local/BLELocalService.cpp"
  "${ARDUINO_BLE_PATH}/src/remote/BLERemoteAttribute.cpp"
  "${ARDUINO_BLE_PATH}/src/remote/BLERemoteCharacteristic.cpp"
  "${ARDUINO_BLE_PATH}/src/remote/BLERemoteDescriptor.cpp"
  "${ARDUINO_BLE_PATH}/src/remote/BLERemoteDevice.cpp"
  "${ARDUINO_BLE_PATH}/src/remote/BLERemoteService.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/ATT.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/BLEHostAlloc.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/BLEUuid.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/GAP.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/GATT.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/HCI.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/HCIVirtualTransport.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/L2CAPSignaling.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/bitDescriptions.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/btct.cpp"
  "${ARDUINO_BLE_PATH}/src/utility/keyDistribution.cpp"
)

idf_component_register(
  SRCS ${ARDUINO_BLE_SRCS}
  INCLUDE_DIRS
    "${ARDUINO_BLE_PATH}/src"
    "${ARDUINO_BLE_PATH}/src/local"
    "${ARDUINO_BLE_PATH}/src/remote"
    "${ARDUINO_BLE_PATH}/src/utility"
  REQUIRES arduino-esp32
  PRIV_REQUIRES bt nvs_flash
)
