#pragma once

#include "ShotStopperPsram.h"

#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

constexpr size_t JSON_ARENA_CAPACITY = 8192;

inline void initJsonArenaHooks();
inline void resetJsonArena();
inline size_t jsonArenaBytesUsed();

namespace detail {

// HTTP JSON parse only. Never use as an NVS/OTA flash I/O buffer: flash writes
// disable the cache and make PSRAM inaccessible on ESP32-S3.
inline size_t g_jsonArenaUsed = 0;
inline bool g_jsonArenaHooksInstalled = false;

inline uint8_t *jsonArenaStorage() {
  static uint8_t *block = nullptr;
  if (block == nullptr) {
    block = static_cast<uint8_t *>(allocExternalOrInternal(JSON_ARENA_CAPACITY));
  }
  return block;
}

inline void *jsonArenaMalloc(size_t size) {
  if (size == 0) {
    return nullptr;
  }
  uint8_t *const storage = jsonArenaStorage();
  if (storage == nullptr) {
    return nullptr;
  }
  const size_t aligned = (size + 7U) & ~size_t(7U);
  if (g_jsonArenaUsed + aligned > JSON_ARENA_CAPACITY) {
    return nullptr;
  }
  void *const block = storage + g_jsonArenaUsed;
  g_jsonArenaUsed += aligned;
  return block;
}

inline void jsonArenaFree(void *) {}

}  // namespace detail

inline void initJsonArenaHooks() {
  if (detail::g_jsonArenaHooksInstalled) {
    return;
  }
  if (detail::jsonArenaStorage() == nullptr) {
    return;
  }
  cJSON_Hooks hooks = {detail::jsonArenaMalloc, detail::jsonArenaFree};
  cJSON_InitHooks(&hooks);
  detail::g_jsonArenaHooksInstalled = true;
}

inline void resetJsonArena() {
  detail::g_jsonArenaUsed = 0;
}

inline size_t jsonArenaBytesUsed() {
  return detail::g_jsonArenaUsed;
}

}  // namespace shotstopper
