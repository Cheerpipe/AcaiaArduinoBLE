#pragma once

#include "ShotStopperDomain.h"

namespace shotstopper {

inline void seedAutoToManualSamples(ShotPreset &preset) {
  resetAutoToManualGuardSamples(preset.autoToManualGuardSamplesDs,
                                preset.autoToManualGuardBaselineMs);
}

inline void fillDoubleFirmwareDefaults(ShotPreset &preset) {
  preset = ShotPreset{};
  preset.brewByWeight = true;
  preset.goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  preset.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  preset.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  preset.weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  preset.weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  preset.fastExtractionGuardEnabled = true;
  preset.maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  preset.minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  preset.slowExtractionGuardEnabled = true;
  preset.minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  preset.maxBbwBrewTimeMs = DEFAULT_MAX_BBW_BREW_TIME_MS;
  preset.autoToManualGuardEnabled = true;
  preset.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  preset.autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  preset.autoToManualGuardBaselineMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
  preset.cupProtectionEnabled = true;
  preset.stopIfCupRemoved = true;
  preset.requireCupToStart = false;
  preset.avoidAccidentalTouchEnabled = true;
  preset.cupPresentWeightG = DEFAULT_CUP_PRESENT_WEIGHT_G;
  preset.cupRemovedWeightG = DEFAULT_CUP_REMOVED_WEIGHT_G;
  seedAutoToManualSamples(preset);
}

inline void fillFactorySinglePreset(ShotPreset &preset) {
  fillDoubleFirmwareDefaults(preset);
  preset.id = FACTORY_PRESET_ID_SINGLE;
  preset.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  preset.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  preset.brewByWeight = true;
  preset.isFactory = true;
  copyCString(preset.name, SHOT_PRESET_NAME_CAPACITY, "Single");
  preset.goalWeightG = FACTORY_SINGLE_GOAL_WEIGHT_G;
  preset.maxRecoveryWeightG = FACTORY_SINGLE_MAX_RECOVERY_WEIGHT_G;
  preset.minBbwBrewTimeMs = FACTORY_SINGLE_MIN_BBW_BREW_TIME_MS;
  preset.minRecoveryWeightG = FACTORY_SINGLE_MIN_RECOVERY_WEIGHT_G;
  preset.maxBbwBrewTimeMs = FACTORY_SINGLE_MAX_BBW_BREW_TIME_MS;
  preset.weightOffsetBaselineG = FACTORY_SINGLE_WEIGHT_OFFSET_G;
  preset.weightOffsetG = FACTORY_SINGLE_WEIGHT_OFFSET_G;
  // Business rule: Single preset defaults keep both extraction guards enabled.
  preset.fastExtractionGuardEnabled = true;
  preset.slowExtractionGuardEnabled = true;
  preset.autoToManualGuardEnabled = true;
  preset.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  preset.autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  preset.autoToManualGuardBaselineMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
}

inline void fillFactoryDoublePreset(ShotPreset &preset) {
  fillDoubleFirmwareDefaults(preset);
  preset.id = FACTORY_PRESET_ID_DOUBLE;
  preset.isFactory = true;
  copyCString(preset.name, SHOT_PRESET_NAME_CAPACITY, "Double");
}

inline void seedDefaultShotPresetBank(ShotPresetBank &bank) {
  bank = ShotPresetBank{};
  bank.count = 2;
  bank.activeId = FACTORY_PRESET_ID_DOUBLE;
  bank.nextId = 3;
  // Double first: primary / default recipe most users run.
  fillFactoryDoublePreset(bank.presets[0]);
  fillFactorySinglePreset(bank.presets[1]);
}

inline int findShotPresetIndex(const ShotPresetBank &bank, uint8_t id) {
  for (uint8_t i = 0; i < bank.count && i < MAX_SHOT_PRESETS; ++i) {
    if (bank.presets[i].id == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

inline void preferDoubleBeforeSingle(ShotPresetBank &bank) {
  const int singleIdx = findShotPresetIndex(bank, FACTORY_PRESET_ID_SINGLE);
  const int doubleIdx = findShotPresetIndex(bank, FACTORY_PRESET_ID_DOUBLE);
  if (singleIdx < 0 || doubleIdx < 0 || doubleIdx < singleIdx) {
    return;
  }
  const ShotPreset tmp = bank.presets[singleIdx];
  bank.presets[singleIdx] = bank.presets[doubleIdx];
  bank.presets[doubleIdx] = tmp;
}

inline ShotPreset *mutableShotPreset(ShotPresetBank &bank, uint8_t id) {
  const int index = findShotPresetIndex(bank, id);
  return index < 0 ? nullptr : &bank.presets[index];
}

inline const ShotPreset *findShotPreset(const ShotPresetBank &bank, uint8_t id) {
  const int index = findShotPresetIndex(bank, id);
  return index < 0 ? nullptr : &bank.presets[index];
}

inline bool shotPresetNameExists(const ShotPresetBank &bank, const char *name,
                                 uint8_t ignoreId) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  for (uint8_t i = 0; i < bank.count && i < MAX_SHOT_PRESETS; ++i) {
    if (bank.presets[i].id == ignoreId) {
      continue;
    }
    if (strncmp(bank.presets[i].name, name, SHOT_PRESET_NAME_CAPACITY) == 0) {
      return true;
    }
  }
  return false;
}

inline bool validShotPresetName(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const size_t length = strnlen(name, SHOT_PRESET_NAME_CAPACITY);
  return length > 0 && length < SHOT_PRESET_NAME_CAPACITY;
}

inline bool validateShotPresetRecipe(const ShotPreset &preset,
                                     uint32_t machineRetareWindowMs,
                                     bool machineAutoRetare) {
  RuntimeConfig probe;
  probe.goalWeightG = preset.goalWeightG;
  probe.weightOffsetG = preset.weightOffsetG;
  probe.weightOffsetBaselineG = preset.weightOffsetBaselineG;
  probe.timerOnly = !preset.brewByWeight;
  probe.bbwProtectionMs = preset.bbwProtectionMs;
  probe.operationalWallMs = preset.operationalWallMs;
  probe.fastExtractionGuardEnabled = preset.fastExtractionGuardEnabled;
  probe.maxRecoveryWeightG = preset.maxRecoveryWeightG;
  probe.minBbwBrewTimeMs = preset.minBbwBrewTimeMs;
  probe.slowExtractionGuardEnabled = preset.slowExtractionGuardEnabled;
  probe.minRecoveryWeightG = preset.minRecoveryWeightG;
  probe.maxBbwBrewTimeMs = preset.maxBbwBrewTimeMs;
  probe.autoToManualGuardEnabled = preset.autoToManualGuardEnabled;
  probe.autoToManualGuardLimitMode = preset.autoToManualGuardLimitMode;
  probe.autoToManualGuardManualLimitMs = preset.autoToManualGuardManualLimitMs;
  probe.autoToManualGuardBaselineMs = preset.autoToManualGuardBaselineMs;
  probe.cupPresentWeightG = preset.cupPresentWeightG;
  probe.cupRemovedWeightG = preset.cupRemovedWeightG;
  probe.autoRetare = machineAutoRetare;
  probe.retareWindowMs = machineRetareWindowMs;
  // Machine defaults for fields validateRuntimeConfig still checks.
  probe.rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  probe.rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  return validateRuntimeConfig(probe) == ConfigValidationError::NONE;
}

inline bool shotPresetBankStructurallyValid(const ShotPresetBank &bank) {
  if (bank.count == 0 || bank.count > MAX_SHOT_PRESETS) {
    return false;
  }
  if (findShotPresetIndex(bank, bank.activeId) < 0) {
    return false;
  }
  for (uint8_t i = 0; i < bank.count; ++i) {
    const ShotPreset &preset = bank.presets[i];
    if (preset.id == 0 || !validShotPresetName(preset.name)) {
      return false;
    }
    for (uint8_t j = i + 1; j < bank.count; ++j) {
      if (bank.presets[j].id == preset.id) {
        return false;
      }
    }
  }
  return true;
}

inline bool validateShotPresetBank(const ShotPresetBank &bank,
                                   uint32_t machineRetareWindowMs,
                                   bool machineAutoRetare) {
  if (!shotPresetBankStructurallyValid(bank)) {
    return false;
  }
  for (uint8_t i = 0; i < bank.count; ++i) {
    if (!validateShotPresetRecipe(bank.presets[i], machineRetareWindowMs,
                                  machineAutoRetare)) {
      return false;
    }
  }
  return true;
}

inline void ensureShotPresetBank(ShotPresetBank &bank,
                                 uint32_t machineRetareWindowMs,
                                 bool machineAutoRetare) {
  if (bank.count == 0 || bank.count > MAX_SHOT_PRESETS) {
    seedDefaultShotPresetBank(bank);
    return;
  }
  preferDoubleBeforeSingle(bank);
  // Invalid activeId: keep customs; retarget to the first slot.
  if (findShotPresetIndex(bank, bank.activeId) < 0) {
    bank.activeId = bank.presets[0].id;
  }
  // Structural repair only — never wipe customs for cross-field clamp issues.
  for (uint8_t i = 0; i < bank.count; ++i) {
    ShotPreset &preset = bank.presets[i];
    if (!validShotPresetName(preset.name)) {
      snprintf(preset.name, SHOT_PRESET_NAME_CAPACITY, "Preset %u",
               static_cast<unsigned>(preset.id));
    }
    if (preset.operationalWallMs < 5000 ||
        preset.operationalWallMs > HARD_MAX_CIRCUIT_CLOSED_MS) {
      preset.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
    }
    if (preset.minBbwBrewTimeMs >= preset.operationalWallMs) {
      preset.minBbwBrewTimeMs = preset.operationalWallMs > 1000
                                 ? preset.operationalWallMs - 1000
                                 : DEFAULT_MIN_BBW_BREW_TIME_MS;
    }
    if (machineAutoRetare &&
        preset.bbwProtectionMs <
            machineRetareWindowMs + MIN_BBW_PROTECTION_AFTER_RETARE_MS) {
      preset.bbwProtectionMs =
          machineRetareWindowMs + MIN_BBW_PROTECTION_AFTER_RETARE_MS;
    }
    if (preset.bbwProtectionMs > preset.operationalWallMs) {
      preset.bbwProtectionMs = preset.operationalWallMs;
    }
    if (preset.minBbwBrewTimeMs < preset.bbwProtectionMs) {
      preset.minBbwBrewTimeMs = preset.bbwProtectionMs;
    }
    if (!isfinite(preset.weightOffsetBaselineG) ||
        preset.weightOffsetBaselineG < 0.0f ||
        preset.weightOffsetBaselineG > MAX_OFFSET_G) {
      preset.weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
    }
    if (!isfinite(preset.weightOffsetG) || preset.weightOffsetG < 0.0f ||
        preset.weightOffsetG > MAX_OFFSET_G) {
      preset.weightOffsetG = preset.weightOffsetBaselineG;
    }
    if (preset.fastExtractionGuardEnabled) {
      if (!isfinite(preset.maxRecoveryWeightG) ||
          preset.maxRecoveryWeightG < MIN_MAX_RECOVERY_WEIGHT_G ||
          preset.maxRecoveryWeightG > MAX_MAX_RECOVERY_WEIGHT_G ||
          preset.maxRecoveryWeightG <=
              static_cast<float>(preset.goalWeightG)) {
        const float repaired =
            static_cast<float>(preset.goalWeightG) + 1.0f;
        if (repaired >= MIN_MAX_RECOVERY_WEIGHT_G &&
            repaired <= MAX_MAX_RECOVERY_WEIGHT_G &&
            repaired > static_cast<float>(preset.goalWeightG)) {
          preset.maxRecoveryWeightG = repaired;
        } else {
          preset.fastExtractionGuardEnabled = false;
        }
      }
      if (preset.fastExtractionGuardEnabled &&
          !validateShotPresetRecipe(preset, machineRetareWindowMs,
                                    machineAutoRetare)) {
        preset.fastExtractionGuardEnabled = false;
      }
    }
    if (preset.slowExtractionGuardEnabled) {
      RuntimeConfig probe = {};
      probe.goalWeightG = preset.goalWeightG;
      probe.operationalWallMs = preset.operationalWallMs;
      probe.bbwProtectionMs = preset.bbwProtectionMs;
      probe.fastExtractionGuardEnabled = preset.fastExtractionGuardEnabled;
      probe.minBbwBrewTimeMs = preset.minBbwBrewTimeMs;
      probe.slowExtractionGuardEnabled = true;
      probe.minRecoveryWeightG = preset.minRecoveryWeightG;
      probe.maxBbwBrewTimeMs = preset.maxBbwBrewTimeMs;
      repairSlowExtractionGuard(probe);
      preset.slowExtractionGuardEnabled = probe.slowExtractionGuardEnabled;
      preset.minRecoveryWeightG = probe.minRecoveryWeightG;
      preset.maxBbwBrewTimeMs = probe.maxBbwBrewTimeMs;
      if (preset.slowExtractionGuardEnabled &&
          !validateShotPresetRecipe(preset, machineRetareWindowMs,
                                    machineAutoRetare)) {
        preset.slowExtractionGuardEnabled = false;
      }
    }
  }
  if (!shotPresetBankStructurallyValid(bank)) {
    // Last resort only when structurally irreparable.
    seedDefaultShotPresetBank(bank);
  }
}

inline const ShotPreset &activeShotPreset(const ShotPresetBank &bank) {
  const ShotPreset *preset = findShotPreset(bank, bank.activeId);
  if (preset != nullptr) {
    return *preset;
  }
  return bank.presets[0];
}

inline ShotPreset &mutableActiveShotPreset(ShotPresetBank &bank) {
  ShotPreset *preset = mutableShotPreset(bank, bank.activeId);
  if (preset != nullptr) {
    return *preset;
  }
  bank.activeId = bank.presets[0].id;
  return bank.presets[0];
}

inline bool setActiveShotPreset(ShotPresetBank &bank, uint8_t id) {
  if (findShotPresetIndex(bank, id) < 0) {
    return false;
  }
  bank.activeId = id;
  return true;
}

inline void applyShotPresetToConfig(const ShotPreset &preset,
                                    RuntimeConfig &config,
                                    bool keepSessionTimerOnly) {
  const bool sessionManual = keepSessionTimerOnly && config.timerOnly;
  config.goalWeightG = preset.goalWeightG;
  config.weightOffsetG = preset.weightOffsetG;
  config.weightOffsetBaselineG = preset.weightOffsetBaselineG;
  config.bbwProtectionMs = preset.bbwProtectionMs;
  config.operationalWallMs = preset.operationalWallMs;
  config.fastExtractionGuardEnabled = preset.fastExtractionGuardEnabled;
  config.maxRecoveryWeightG = preset.maxRecoveryWeightG;
  config.minBbwBrewTimeMs = preset.minBbwBrewTimeMs;
  config.slowExtractionGuardEnabled = preset.slowExtractionGuardEnabled;
  config.minRecoveryWeightG = preset.minRecoveryWeightG;
  config.maxBbwBrewTimeMs = preset.maxBbwBrewTimeMs;
  config.autoToManualGuardEnabled = preset.autoToManualGuardEnabled;
  config.autoToManualGuardLimitMode = preset.autoToManualGuardLimitMode;
  config.autoToManualGuardManualLimitMs = preset.autoToManualGuardManualLimitMs;
  config.autoToManualGuardBaselineMs = preset.autoToManualGuardBaselineMs;
  config.cupProtectionEnabled = preset.cupProtectionEnabled;
  config.stopIfCupRemoved = preset.stopIfCupRemoved;
  config.requireCupToStart = preset.requireCupToStart;
  config.avoidAccidentalTouchEnabled = preset.avoidAccidentalTouchEnabled;
  memcpy(config.autoToManualGuardSamplesDs, preset.autoToManualGuardSamplesDs,
         sizeof(config.autoToManualGuardSamplesDs));
  config.timerOnly = sessionManual ? true : !preset.brewByWeight;
}

inline RuntimeConfig composeEffectiveConfig(const RuntimeConfig &machine,
                                            const ShotPresetBank &bank) {
  RuntimeConfig effective = machine;
  applyShotPresetToConfig(activeShotPreset(bank), effective, true);
  return effective;
}

inline void copyUserRecipeFromConfig(const RuntimeConfig &config,
                                     ShotPreset &preset) {
  preset.brewByWeight = !config.timerOnly;
  preset.goalWeightG = config.goalWeightG;
  preset.operationalWallMs = config.operationalWallMs;
  preset.bbwProtectionMs = config.bbwProtectionMs;
  preset.weightOffsetBaselineG = config.weightOffsetBaselineG;
  // Do not overwrite learned weightOffsetG or A→M samples here.
  preset.fastExtractionGuardEnabled = config.fastExtractionGuardEnabled;
  preset.maxRecoveryWeightG = config.maxRecoveryWeightG;
  preset.minBbwBrewTimeMs = config.minBbwBrewTimeMs;
  preset.slowExtractionGuardEnabled = config.slowExtractionGuardEnabled;
  preset.minRecoveryWeightG = config.minRecoveryWeightG;
  preset.maxBbwBrewTimeMs = config.maxBbwBrewTimeMs;
  preset.autoToManualGuardEnabled = config.autoToManualGuardEnabled;
  preset.autoToManualGuardLimitMode = config.autoToManualGuardLimitMode;
  preset.autoToManualGuardManualLimitMs = config.autoToManualGuardManualLimitMs;
  preset.autoToManualGuardBaselineMs = config.autoToManualGuardBaselineMs;
  preset.cupProtectionEnabled = config.cupProtectionEnabled;
  preset.stopIfCupRemoved = config.stopIfCupRemoved;
  preset.requireCupToStart = config.requireCupToStart;
  preset.avoidAccidentalTouchEnabled = config.avoidAccidentalTouchEnabled;
}

inline bool allocateShotPresetId(ShotPresetBank &bank, uint8_t &outId) {
  for (uint16_t attempt = 0; attempt < 256; ++attempt) {
    const uint8_t candidate = bank.nextId == 0 ? 1 : bank.nextId;
    bank.nextId = static_cast<uint8_t>(candidate + 1);
    if (candidate == 0) {
      continue;
    }
    if (findShotPresetIndex(bank, candidate) < 0) {
      outId = candidate;
      return true;
    }
  }
  return false;
}

inline bool createUntitledShotPreset(ShotPresetBank &bank, uint8_t &outId) {
  if (bank.count >= MAX_SHOT_PRESETS) {
    return false;
  }
  uint8_t id = 0;
  if (!allocateShotPresetId(bank, id)) {
    return false;
  }
  ShotPreset &slot = bank.presets[bank.count];
  fillDoubleFirmwareDefaults(slot);
  slot.id = id;
  slot.isFactory = false;
  copyCString(slot.name, SHOT_PRESET_NAME_CAPACITY, "Untitled");
  if (shotPresetNameExists(bank, slot.name, 0)) {
    for (uint8_t n = 2; n < 100; ++n) {
      snprintf(slot.name, SHOT_PRESET_NAME_CAPACITY, "Untitled %u",
               static_cast<unsigned>(n));
      if (!shotPresetNameExists(bank, slot.name, 0)) {
        break;
      }
    }
  }
  ++bank.count;
  bank.activeId = id;
  outId = id;
  return true;
}

inline void stripCopySuffix(const char *name, char *base, size_t baseCap) {
  if (baseCap == 0) {
    return;
  }
  copyCString(base, baseCap, name != nullptr ? name : "");
  char *copyToken = strstr(base, " copy");
  if (copyToken == nullptr) {
    return;
  }
  const char *after = copyToken + 5;
  if (*after == '\0') {
    *copyToken = '\0';
    return;
  }
  if (*after == ' ') {
    ++after;
    bool digits = *after != '\0';
    for (const char *p = after; *p; ++p) {
      if (*p < '0' || *p > '9') {
        digits = false;
        break;
      }
    }
    if (digits) {
      *copyToken = '\0';
    }
  }
}

inline bool makeDuplicatePresetName(const ShotPresetBank &bank,
                                    const char *sourceName, char *outName,
                                    size_t outCap) {
  if (outCap == 0) {
    return false;
  }
  char base[SHOT_PRESET_NAME_CAPACITY];
  stripCopySuffix(sourceName, base, sizeof(base));
  if (base[0] == '\0') {
    copyCString(base, sizeof(base), "Preset");
  }
  // Longest suffix is " copy 99" (8) + NUL. Cap base so snprintf cannot
  // truncate under GCC -O2 -Wformat-truncation.
  constexpr size_t kSuffixRoom = 9;
  if (outCap <= kSuffixRoom) {
    return false;
  }
  const size_t maxBase = outCap - kSuffixRoom;
  if (strnlen(base, sizeof(base)) > maxBase) {
    base[maxBase] = '\0';
  }
  for (uint8_t n = 1; n < 100; ++n) {
    if (n == 1) {
      snprintf(outName, outCap, "%s copy", base);
    } else {
      snprintf(outName, outCap, "%s copy %u", base, static_cast<unsigned>(n));
    }
    if (!shotPresetNameExists(bank, outName, 0)) {
      return true;
    }
  }
  return false;
}

inline bool duplicateShotPreset(ShotPresetBank &bank, uint8_t sourceId,
                                uint8_t &outId) {
  if (bank.count >= MAX_SHOT_PRESETS) {
    return false;
  }
  const ShotPreset *source = findShotPreset(bank, sourceId);
  if (source == nullptr) {
    return false;
  }
  const uint8_t savedNextId = bank.nextId;
  uint8_t id = 0;
  if (!allocateShotPresetId(bank, id)) {
    return false;
  }
  ShotPreset &slot = bank.presets[bank.count];
  slot = *source;
  slot.id = id;
  slot.isFactory = false;
  if (!makeDuplicatePresetName(bank, source->name, slot.name,
                               SHOT_PRESET_NAME_CAPACITY)) {
    slot = ShotPreset{};
    bank.nextId = savedNextId;
    return false;
  }
  ++bank.count;
  bank.activeId = id;
  outId = id;
  return true;
}

inline bool renameShotPreset(ShotPresetBank &bank, uint8_t id,
                             const char *newName) {
  if (!validShotPresetName(newName) ||
      shotPresetNameExists(bank, newName, id)) {
    return false;
  }
  ShotPreset *preset = mutableShotPreset(bank, id);
  if (preset == nullptr) {
    return false;
  }
  memset(preset->name, 0, sizeof(preset->name));
  copyCString(preset->name, SHOT_PRESET_NAME_CAPACITY, newName);
  return true;
}

inline bool deleteShotPreset(ShotPresetBank &bank, uint8_t id) {
  if (bank.count <= 1) {
    return false;
  }
  const int index = findShotPresetIndex(bank, id);
  if (index < 0) {
    return false;
  }
  // Factory Single/Double are permanent slots — edit or restore only.
  if (bank.presets[index].isFactory || id == FACTORY_PRESET_ID_SINGLE ||
      id == FACTORY_PRESET_ID_DOUBLE) {
    return false;
  }
  for (uint8_t i = static_cast<uint8_t>(index); i + 1 < bank.count; ++i) {
    bank.presets[i] = bank.presets[i + 1];
  }
  --bank.count;
  bank.presets[bank.count] = ShotPreset{};
  if (bank.activeId == id) {
    bank.activeId = bank.presets[0].id;
  }
  return true;
}

inline bool restoreFactoryShotPresetValues(ShotPresetBank &bank, uint8_t id) {
  ShotPreset *preset = mutableShotPreset(bank, id);
  if (preset == nullptr || !preset->isFactory) {
    return false;
  }
  char name[SHOT_PRESET_NAME_CAPACITY];
  memcpy(name, preset->name, sizeof(name));
  if (id == FACTORY_PRESET_ID_SINGLE) {
    fillFactorySinglePreset(*preset);
    preset->fastExtractionGuardEnabled = true;
    preset->slowExtractionGuardEnabled = true;
  } else if (id == FACTORY_PRESET_ID_DOUBLE) {
    fillFactoryDoublePreset(*preset);
  } else {
    return false;
  }
  // Keep user-facing name.
  memcpy(preset->name, name, sizeof(preset->name));
  preset->id = id;
  preset->isFactory = true;
  return true;
}

inline void migrateRecipeFromRuntimeToBank(const RuntimeConfig &runtime,
                                           ShotPresetBank &bank) {
  seedDefaultShotPresetBank(bank);
  ShotPreset *dbl = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE);
  if (dbl == nullptr) {
    return;
  }
  dbl->goalWeightG = runtime.goalWeightG;
  dbl->operationalWallMs = runtime.operationalWallMs;
  dbl->bbwProtectionMs = runtime.bbwProtectionMs;
  dbl->weightOffsetBaselineG = runtime.weightOffsetBaselineG;
  dbl->weightOffsetG = runtime.weightOffsetG;
  dbl->fastExtractionGuardEnabled = runtime.fastExtractionGuardEnabled;
  dbl->maxRecoveryWeightG = runtime.maxRecoveryWeightG;
  dbl->minBbwBrewTimeMs = runtime.minBbwBrewTimeMs;
  dbl->slowExtractionGuardEnabled = runtime.slowExtractionGuardEnabled;
  dbl->minRecoveryWeightG = runtime.minRecoveryWeightG;
  dbl->maxBbwBrewTimeMs = runtime.maxBbwBrewTimeMs;
  dbl->autoToManualGuardEnabled = runtime.autoToManualGuardEnabled;
  dbl->autoToManualGuardLimitMode = runtime.autoToManualGuardLimitMode;
  dbl->autoToManualGuardManualLimitMs = runtime.autoToManualGuardManualLimitMs;
  dbl->autoToManualGuardBaselineMs = runtime.autoToManualGuardBaselineMs;
  memcpy(dbl->autoToManualGuardSamplesDs, runtime.autoToManualGuardSamplesDs,
         sizeof(dbl->autoToManualGuardSamplesDs));
  dbl->cupProtectionEnabled = runtime.cupProtectionEnabled;
  dbl->stopIfCupRemoved = runtime.stopIfCupRemoved;
  dbl->requireCupToStart = runtime.requireCupToStart;
  dbl->avoidAccidentalTouchEnabled = runtime.avoidAccidentalTouchEnabled;
  dbl->cupPresentWeightG = runtime.cupPresentWeightG;
  dbl->cupRemovedWeightG = runtime.cupRemovedWeightG;
  dbl->brewByWeight = !runtime.timerOnly;
}

}  // namespace shotstopper
