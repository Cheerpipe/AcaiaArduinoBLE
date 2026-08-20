idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  PRIV_REQUIRES
    shotStopper
    arduino-esp32
)

# Keep g_probe / shotstopperKeepBssProbe in the ELF for ss_idf_verify_firmware.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u shotstopperKeepBssProbe")
