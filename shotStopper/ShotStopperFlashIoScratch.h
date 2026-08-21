#pragma once

// Shared internal-SRAM workspace for Preferences getBytes/putBytes while the
// flash cache may be disabled (PSRAM is then inaccessible on ESP32-S3).
// Settings dual-slot I/O and shot-log load/compact reuse the same bytes; both
// paths must hold tryLockFlashIo() for the whole use of the scratch.

#include "ShotStopperDomain.h"
#include "ShotStopperPsram.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_memory_utils.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

// Large enough for 2× PersistedSettings (NVS budget) or one ShotLogStore
// (120×48 records + header ≈ 5784 B). Keep in sync with static_asserts at the
// call sites that reinterpret these bytes.
constexpr size_t FLASH_IO_SCRATCH_BYTES = 2 * PERSISTED_SETTINGS_NVS_BUDGET;
constexpr uint32_t FLASH_IO_LOCK_TIMEOUT_MS = 5000;
// Control-loop NVS must fail fast: waiting the full durable timeout equals the
// task watchdog budget and can panic mid-pour.
constexpr uint32_t FLASH_IO_CONTROL_LOCK_TIMEOUT_MS = 50;

static_assert(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS < FLASH_IO_LOCK_TIMEOUT_MS,
              "Control flash lock must be shorter than durable I/O");

inline uint8_t *&flashIoScratchBlock() {
  static uint8_t *block = nullptr;
  return block;
}

// Internal SRAM, heap-accounted. Do not use BSS: ALLOW_BSS would put 6 KiB in
// PSRAM (SET_WIFI load fails), and a forced .dram0.bss object can overlap the
// heap (IWDT on a corrupted malloc spinlock, magic 0x53544F50 / "STOP").
inline uint8_t *flashIoScratchBytes() {
  return flashIoScratchBlock();
}

inline bool ensureFlashIoScratch() {
  uint8_t *&block = flashIoScratchBlock();
  if (block != nullptr) {
    return true;
  }
  block = static_cast<uint8_t *>(allocInternal(FLASH_IO_SCRATCH_BYTES));
  if (block == nullptr) {
    return false;
  }
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  if (!esp_ptr_internal(block)) {
    heapCapsFree(block);
    block = nullptr;
    return false;
  }
#endif
  memset(block, 0, FLASH_IO_SCRATCH_BYTES);
  return true;
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
    return ensureFlashIoScratch();
  }
  if (xSemaphoreTakeRecursive(handle, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
    if (!ensureFlashIoScratch()) {
      xSemaphoreGiveRecursive(handle);
      return false;
    }
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
inline bool tryLockFlashIo(uint32_t = FLASH_IO_LOCK_TIMEOUT_MS) {
  return ensureFlashIoScratch();
}
inline void unlockFlashIo() {}
#endif

inline void yieldFlashIo() {
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  vTaskDelay(pdMS_TO_TICKS(1));
#endif
}

inline void feedFlashIoWatchdog() {
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  // HTTP workers are not TWDT subscribers. Resetting from them logs
  // "esp_task_wdt_reset: task not found" and does not feed loop/network/scale.
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    (void)esp_task_wdt_reset();
  }
#endif
}

// Compatibility alias used by existing call sites.
inline bool lockFlashIo() { return tryLockFlashIo(); }
inline bool lockFlashIoForControl() {
  return tryLockFlashIo(FLASH_IO_CONTROL_LOCK_TIMEOUT_MS);
}

// Preferences putBytes/getBytes must not touch PSRAM while flash cache is
// off. Caller must already hold the flash I/O lock so the scratch exists.
inline void *copyToFlashIoScratch(const void *source, size_t bytes) {
  if (source == nullptr || bytes == 0 || bytes > FLASH_IO_SCRATCH_BYTES) {
    return nullptr;
  }
  uint8_t *scratch = flashIoScratchBytes();
  if (scratch == nullptr) {
    return nullptr;
  }
  memcpy(scratch, source, bytes);
  return scratch;
}

}  // namespace shotstopper
