#pragma once

#include "ShotStopperBleCompanionPersistence.h"
#include "ShotStopperLastShot.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperShotCurve.h"
#include "ShotStopperShotLog.h"

namespace shotstopper {

inline bool verifyFactorySettings(const PersistedSettings &settings) {
  return validPersistedSettings(settings) && !settings.staConfigured &&
         !settings.lkgValid && passwordIsFactoryDefault(settings) &&
         settings.preferredScaleMac[0] == '\0' &&
         settings.preferredScaleName[0] == '\0';
}

inline bool resetPersistedNetworkAccess(PersistedSettings &settings) {
  PersistedSettings candidate;
  if (!loadPersistedSettings(candidate) &&
      !initializeDefaultSettings(candidate)) {
    return false;
  }
  clearStaNetwork(candidate);
  if (!initializeDefaultDevicePassword(candidate) ||
      !savePersistedSettings(candidate)) {
    return false;
  }
  PersistedSettings verified;
  if (!loadPersistedSettings(verified) || verified.staConfigured ||
      verified.lkgValid ||
      verified.staIpMode != static_cast<uint8_t>(StaIpMode::DHCP) ||
      !passwordIsFactoryDefault(verified)) {
    return false;
  }
  settings = verified;
  return true;
}

// Independent stores first, settings next (clears the shared namespace), BLE
// last. Every store is verified before success. Idempotent.
// Orchestrator last-shot UI snapshot (`persistedLastShot`) is not this
// store: callers that publish status must drop it after this returns true.
inline bool resetAllDurableStores(PersistedSettings &settings,
                                  BleCompanionPersistedSettings &ble,
                                  ShotLog &shotLog,
                                  LastShotStore &lastShot,
                                  ShotCurveLog &shotCurves) {
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!shotLog.clear() || !shotCurves.clear() || !lastShot.clear()) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetPersistedSettingsToFactory(settings)) {
    return false;
  }
  yieldFlashIo();
  feedFlashIoWatchdog();
  if (!resetBleCompanionSettings(ble)) {
    return false;
  }

  yieldFlashIo();
  feedFlashIoWatchdog();
  PersistedSettings verifiedSettings;
  BleCompanionPersistedSettings verifiedBle;
  const bool shotLogVerified = shotLog.load() && shotLog.count() == 0;
  const bool shotCurvesVerified = shotCurves.load() && shotCurves.count() == 0;
  const bool lastShotVerified = lastShot.load() && !lastShot.get().valid;
  return loadPersistedSettings(verifiedSettings) &&
         verifyFactorySettings(verifiedSettings) &&
         loadBleCompanionSettings(verifiedBle) && verifiedBle.enabled == 1 &&
         shotLogVerified && shotCurvesVerified && lastShotVerified;
}

}  // namespace shotstopper
