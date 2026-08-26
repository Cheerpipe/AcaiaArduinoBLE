set(ESPRESSO_SCALE_BLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/EspressoScaleBLE")

idf_component_register(
  SRCS
    "${ESPRESSO_SCALE_BLE_DIR}/src/EspressoScaleBLE.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/ScaleProtocolCommon.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Registry.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Acaia.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/GenericFf11.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Felicita.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Eclair.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Decent.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Difluid.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Myscale.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/WeighMyBru.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Varia.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Eureka.cpp"
  INCLUDE_DIRS "${ESPRESSO_SCALE_BLE_DIR}/src"
  REQUIRES arduino-esp32 ArduinoBLE
)
