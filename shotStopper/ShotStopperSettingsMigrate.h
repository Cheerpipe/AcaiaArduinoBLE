#pragma once

// In-place settings upgrades on the current schema. No Preferences / NVS.

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
