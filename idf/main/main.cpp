#include "Arduino.h"
#include "esp_attr.h"

// 8 KiB BSS probe. With ALLOW_BSS enabled this must land in PSRAM
// (typically 0x3c0xxxxx). On Arduino-ESP32 the same attribute is a no-op
// and the linker would put it in internal DRAM (0x3fcxxxxx).
extern "C" {
EXT_RAM_BSS_ATTR uint8_t g_probe[8192];

// Arduino core only defines this flag when Bluedroid or NimBLE is the host.
// ArduinoBLE talks to the controller over VHCI (CONFIG_BT_CONTROLLER_ONLY),
// but esp32-hal-alloc-ble-mem.h still references the symbol.
bool _bleLibraryInUse = false;
}

// Keep g_probe in the ELF so ./scripts/build-idf can still prove ALLOW_BSS.
extern "C" void shotstopperKeepBssProbe(void) {
  g_probe[0] = static_cast<uint8_t>(g_probe[0] + 1U);
}

#include "shotStopper.ino"
