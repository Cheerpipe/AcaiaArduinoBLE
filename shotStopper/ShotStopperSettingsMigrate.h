#pragma once

// Settings schema migrations placeholder.
//
// Current on-disk schema is CONFIG_SCHEMA_VERSION (V1). There is no upgrade
// path from any prior layout: unrecognized blobs are rejected and factory
// defaults are used instead.
//
// When bumping CONFIG_SCHEMA_VERSION:
// 1. Keep the previous blob layout/size as a named legacy constant.
// 2. Add migratePersistedSettingsFromV<N>(...) that validates magic, version,
//    size, and checksum, then maps into PersistedSettings.
// 3. Call it from readSettingsSlot() for that storedLength / version.
// 4. Cover the path in persistence_host_test.cpp.

#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPresets.h"

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

}  // namespace shotstopper
