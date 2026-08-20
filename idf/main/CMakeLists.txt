set(SHOT_STOPPER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../shotStopper")

idf_component_register(
  SRCS
    "main.cpp"
    "${SHOT_STOPPER_DIR}/ShotStopperNetwork.cpp"
    "${SHOT_STOPPER_DIR}/ShotStopperOta.cpp"
  INCLUDE_DIRS
    "."
    "${SHOT_STOPPER_DIR}"
  REQUIRES
    arduino-esp32
    AcaiaArduinoBLE
    json
    nvs_flash
    esp_wifi
    esp_timer
    driver
    bt
    spi_flash
    app_update
    esp_http_server
    esp_partition
    esp_hw_support
    heap
    soc
    espressif__network_provisioning
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u shotstopperKeepBssProbe")
