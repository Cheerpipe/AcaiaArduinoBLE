#pragma once

#include <stdbool.h>
#include <stdint.h>

enum class ShotStopperBleRuntimeState : uint8_t {
  Stopped,
  Starting,
  Ready,
  Unsynced,
  Failed,
  Stopping
};

struct ShotStopperBleHealth {
  ShotStopperBleRuntimeState state;
  int32_t lastError;
  int32_t lastResetReason;
  uint32_t syncGeneration;
  uint32_t resetCount;
  uint32_t hostTaskStackHighWaterWords;
  uint32_t internalFreeBytes;
  uint32_t internalMinimumFreeBytes;
  uint32_t internalLargestBlockBytes;
  uint32_t psramFreeBytes;
  uint32_t psramMinimumFreeBytes;
  uint32_t psramLargestBlockBytes;
};

using ShotStopperBleGattRegistration = int (*)(void *context);

// Installs the optional application GATT profile registration hook. It is
// called after nimble_port_init() and the standard GAP/GATT services, but
// before the host task starts. Configuration is immutable once start begins.
bool shotStopperBleRuntimeConfigureGattProfile(
    ShotStopperBleGattRegistration registration, void *context);

// Starts the single process-wide NimBLE host and waits for the sync callback.
// NVS must already be initialized by the firmware. Safe to call repeatedly.
bool shotStopperBleRuntimeStart(uint32_t timeoutMs);

bool shotStopperBleRuntimeReady();
uint8_t shotStopperBleRuntimeOwnAddressType();
uint32_t shotStopperBleRuntimeSyncGeneration();
ShotStopperBleHealth shotStopperBleRuntimeHealth();

// Intended for orderly reboot/shutdown only. Peripheral reconnects must never
// cycle the global host.
bool shotStopperBleRuntimeStop(uint32_t timeoutMs);
