set(ESPRESSO_SCALE_BLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/EspressoScaleBLE")

if(CONFIG_ESPRESSO_SCALE_BLE_BACKEND_NIMBLE)
  message(FATAL_ERROR "The NimBLE scale backend is a guarded phase-1 stub; select CONFIG_ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE until phase 2")
endif()

set(ESPRESSO_SCALE_BLE_REQUIRES arduino-esp32 log ArduinoBLE)

idf_component_register(
  SRCS
    "${ESPRESSO_SCALE_BLE_DIR}/src/EspressoScaleBLE.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/ScaleBleStateMachine.cpp"
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
  REQUIRES ${ESPRESSO_SCALE_BLE_REQUIRES}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE=1)
