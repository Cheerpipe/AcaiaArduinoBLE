set(ACAIA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../libraries/AcaiaArduinoBLE")

# ArduinoBLE / arduino-esp32 are public: AcaiaArduinoBLE.h includes their headers.
idf_component_register(
  SRCS "${ACAIA_DIR}/AcaiaArduinoBLE.cpp"
  INCLUDE_DIRS "${ACAIA_DIR}"
  REQUIRES arduino-esp32 ArduinoBLE
)
