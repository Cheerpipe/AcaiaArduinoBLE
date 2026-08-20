#pragma once

// Shared internal-SRAM workspace for Preferences getBytes/putBytes while the
// flash cache may be disabled (PSRAM is then inaccessible on ESP32-S3).
// Settings dual-slot I/O and shot-log load/compact reuse the same bytes; both
// paths must hold tryLockFlashIo() for the whole use of the scratch.

#include "ShotStopperDomain.h"

#include <stddef.h>
#include <stdint.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace shotstopper {

// Large enough for 2× PersistedSettings (NVS budget) or one ShotLogStore
// (120×48 records + header ≈ 5784 B). Keep in sync with static_asserts at the
// call sites that reinterpret these bytes.
constexpr size_t FLASH_IO_SCRATCH_BYTES = 2 * PERSISTED_SETTINGS_NVS_BUDGET;
constexpr uint32_t FLASH_IO_LOCK_TIMEOUT_MS = 5000;

inline uint8_t *flashIoScratchBytes() {
  struct alignas(4) Storage {
    uint8_t bytes[FLASH_IO_SCRATCH_BYTES];
  };
  static Storage storage = {};
  return storage.bytes;
}

inline uint32_t &flashIoLockTimeoutCount() {
  static uint32_t count = 0;
  return count;
}

inline uint32_t flashIoLockTimeouts() { return flashIoLockTimeoutCount(); }

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
inline SemaphoreHandle_t flashIoMutexHandle() {
  static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutex();
  return handle;
}

inline bool tryLockFlashIo(uint32_t timeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
  SemaphoreHandle_t handle = flashIoMutexHandle();
  if (handle == nullptr) {
    return true;
  }
  if (xSemaphoreTakeRecursive(handle, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
    return true;
  }
  ++flashIoLockTimeoutCount();
  return false;
}

inline void unlockFlashIo() {
  SemaphoreHandle_t handle = flashIoMutexHandle();
  if (handle != nullptr) {
    xSemaphoreGiveRecursive(handle);
  }
}
#else
inline bool tryLockFlashIo(uint32_t = FLASH_IO_LOCK_TIMEOUT_MS) { return true; }
inline void unlockFlashIo() {}
#endif

// Compatibility alias used by existing call sites.
inline bool lockFlashIo() { return tryLockFlashIo(); }

}  // namespace shotstopper
