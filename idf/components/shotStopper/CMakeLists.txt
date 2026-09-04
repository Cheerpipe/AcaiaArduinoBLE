# Application firmware sources live in ../../shotStopper. This component is
# their ESP-IDF registration.
set(SHOT_STOPPER_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../shotStopper")

set(SHOT_STOPPER_SRCS
  "${SHOT_STOPPER_DIR}/shotStopper.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperScaleWorker.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperNetwork.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperOta.cpp"
  "${SHOT_STOPPER_DIR}/ShotStopperWebhook.cpp"
  "${SHOT_STOPPER_DIR}/ble/ShotStopperBleCompanionNimble.cpp")

idf_component_register(
  SRCS ${SHOT_STOPPER_SRCS}
  INCLUDE_DIRS
    "${SHOT_STOPPER_DIR}"
  REQUIRES
    arduino-esp32
    EspressoScaleBLE
    ShotStopperBleRuntime
  PRIV_REQUIRES
    json
    nvs_flash
    esp_wifi
    esp_timer
    driver
    bt
    spi_flash
    app_update
    esp_http_server
    esp_http_client
    esp_partition
    esp_hw_support
    log
    heap
    soc
    # WiFiGeneric.h includes this even when CONFIG_ARDUINO_SELECTIVE_WiFiProv
    # is unset (selective compilation). main used to get it via auto-REQUIRES.
    espressif__network_provisioning
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
