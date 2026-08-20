set(ACAIA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/AcaiaArduinoBLE")

idf_component_register(
  SRCS "${ACAIA_DIR}/AcaiaArduinoBLE.cpp"
  INCLUDE_DIRS "${ACAIA_DIR}"
  REQUIRES arduino-esp32 ArduinoBLE
)
