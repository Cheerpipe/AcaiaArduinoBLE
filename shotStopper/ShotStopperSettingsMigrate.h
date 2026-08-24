#pragma once

// In-place settings upgrades on the current schema. No Preferences / NVS.

#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPresets.h"

#include <string.h>

namespace shotstopper {

inline void ensurePersistedPresetBank(PersistedSettings &settings) {
  // Only migrate recipe→bank when empty. Invalid activeId is repaired in
  // ensureShotPresetBank without wiping customs or copying session Manual.
  if (settings.presets.count == 0) {
    migrateRecipeFromRuntimeToBank(settings.runtime, settings.presets);
  }
  ensureShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                       settings.runtime.autoRetare);
}

inline bool migratePersistedSettingsFromV11(const uint8_t *raw, size_t length,
                                            PersistedSettings &out) {
  if (raw == nullptr || length != PERSISTED_SETTINGS_V11_SIZE) {
    return false;
  }
  PersistedSettingsHeader header = {};
  memcpy(&header, raw, sizeof(header));
  if (header.magic != PERSISTED_SETTINGS_MAGIC ||
      header.schemaVersion != CONFIG_SCHEMA_VERSION_V11 ||
      header.structureSize != PERSISTED_SETTINGS_V11_SIZE) {
    return false;
  }
  const size_t v11ChecksumOffset =
      PERSISTED_SETTINGS_V11_SIZE - sizeof(uint32_t);
  uint32_t storedChecksum = 0;
  memcpy(&storedChecksum, raw + v11ChecksumOffset, sizeof(storedChecksum));
  if (storedChecksum != crc32(raw, v11ChecksumOffset)) {
    return false;
  }

  out = PersistedSettings{};
  memcpy(&out, raw, sizeof(PersistedSettingsHeader));
  memcpy(&out.runtime, raw + sizeof(PersistedSettingsHeader),
         RUNTIME_CONFIG_V11_SIZE);
  out.runtime.momentaryStartOnPress = true;
  out.runtime.reedConfirmTimeoutHundredMs = 0;
  const size_t v11TailOffset =
      sizeof(PersistedSettingsHeader) + RUNTIME_CONFIG_V11_SIZE;
  const size_t v12TailOffset =
      sizeof(PersistedSettingsHeader) + sizeof(RuntimeConfig);
  const size_t tailBytes = PERSISTED_SETTINGS_V11_SIZE - v11TailOffset;
  memcpy(reinterpret_cast<uint8_t *>(&out) + v12TailOffset, raw + v11TailOffset,
         tailBytes);
  out.schemaVersion = CONFIG_SCHEMA_VERSION;
  out.structureSize = sizeof(PersistedSettings);
  out.checksum = 0;
  out.checksum = persistedSettingsChecksum(out);
  return true;
}

}  // namespace shotstopper
