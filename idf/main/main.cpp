#include "Arduino.h"
#include "esp_attr.h"

// Small BSS probe. With ALLOW_BSS enabled this must land in PSRAM
// (typically 0x3c0xxxxx). On Arduino-ESP32 the same attribute is a no-op
// and the linker would put it in internal DRAM (0x3fcxxxxx). Sized just
// large enough for ./scripts/build-idf to verify placement — not 8 KiB.
extern "C" {
EXT_RAM_BSS_ATTR uint8_t g_probe[256];
}

// Keep g_probe in the ELF so ./scripts/build-idf can still prove ALLOW_BSS.
extern "C" void shotstopperKeepBssProbe(void) {
  g_probe[0] = static_cast<uint8_t>(g_probe[0] + 1U);
}

// Application setup()/loop() live in the shotStopper component
// (shotStopper/shotStopper.cpp), started by CONFIG_AUTOSTART_ARDUINO.
