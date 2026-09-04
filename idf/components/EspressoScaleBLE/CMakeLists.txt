set(ESPRESSO_SCALE_BLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/EspressoScaleBLE")

set(ESPRESSO_SCALE_BLE_SRCS
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
  "${ESPRESSO_SCALE_BLE_DIR}/src/protocols/Eureka.cpp")

if(CONFIG_ESPRESSO_SCALE_BLE_BACKEND_NIMBLE)
  list(APPEND ESPRESSO_SCALE_BLE_SRCS
    "${ESPRESSO_SCALE_BLE_DIR}/src/EspressoScaleBLENimble.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/nimble/NimbleAdvertisement.cpp"
    "${ESPRESSO_SCALE_BLE_DIR}/src/nimble/NimbleResilience.cpp")
  set(ESPRESSO_SCALE_BLE_BACKEND_DEFINE
    ESPRESSO_SCALE_BLE_BACKEND_NIMBLE=1)
else()
  list(APPEND ESPRESSO_SCALE_BLE_SRCS
    "${ESPRESSO_SCALE_BLE_DIR}/src/EspressoScaleBLE.cpp")
  set(ESPRESSO_SCALE_BLE_BACKEND_DEFINE
    ESPRESSO_SCALE_BLE_BACKEND_ARDUINOBLE=1)
endif()

# Component requirements are collected by IDF before sdkconfig is loaded, so
# they cannot be conditional on CONFIG_*. Source selection below is conditional
# in the real configure pass; unused backend archives are dead-stripped.
set(ESPRESSO_SCALE_BLE_REQUIRES
  arduino-esp32 log bt ArduinoBLE ShotStopperBleRuntime)

idf_component_register(
  SRCS ${ESPRESSO_SCALE_BLE_SRCS}
  INCLUDE_DIRS "${ESPRESSO_SCALE_BLE_DIR}/src"
  REQUIRES ${ESPRESSO_SCALE_BLE_REQUIRES}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
  ${ESPRESSO_SCALE_BLE_BACKEND_DEFINE})
