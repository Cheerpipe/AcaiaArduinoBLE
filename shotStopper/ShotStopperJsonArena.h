#pragma once

#include "ShotStopperPsram.h"

#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

constexpr size_t JSON_ARENA_CAPACITY = 16384;

inline void initJsonArenaHooks();
inline bool jsonArenaHooksInstalled();
inline cJSON *parseJsonInArena(const char *body);
inline void resetJsonArena();
inline size_t jsonArenaBytesUsed();
inline uint32_t jsonArenaAllocFailures();
inline bool jsonArenaExhaustedRecently();

namespace detail {

// HTTP JSON parse only. Never use as an NVS/OTA flash I/O buffer: flash writes
// disable the cache and make PSRAM inaccessible on ESP32-S3.
inline size_t g_jsonArenaUsed = 0;
inline uint32_t g_jsonArenaAllocFailures = 0;
inline bool g_jsonArenaHooksInstalled = false;

inline uint8_t *jsonArenaStorage() {
  static uint8_t *block = nullptr;
  if (block == nullptr) {
    block = static_cast<uint8_t *>(allocExternalOrInternal(JSON_ARENA_CAPACITY));
  }
  return block;
}

inline void *jsonArenaMalloc(size_t size) {
  if (size == 0 || size > JSON_ARENA_CAPACITY) {
    if (size > JSON_ARENA_CAPACITY) {
      ++g_jsonArenaAllocFailures;
    }
    return nullptr;
  }
  uint8_t *const storage = jsonArenaStorage();
  if (storage == nullptr) {
    ++g_jsonArenaAllocFailures;
    return nullptr;
  }
  const size_t aligned = (size + 7U) & ~size_t(7U);
  if (g_jsonArenaUsed > JSON_ARENA_CAPACITY ||
      aligned > JSON_ARENA_CAPACITY - g_jsonArenaUsed) {
    ++g_jsonArenaAllocFailures;
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

inline bool jsonArenaHooksInstalled() {
  return detail::g_jsonArenaHooksInstalled;
}

// Fail closed: never parse through the process-global cJSON heap if the
// bump arena could not be installed. HTTP JSON is httpd-task only.
inline cJSON *parseJsonInArena(const char *body) {
  resetJsonArena();
  if (body == nullptr || !detail::g_jsonArenaHooksInstalled) {
    return nullptr;
  }
  return cJSON_Parse(body);
}

#ifdef SHOT_STOPPER_HOST_TEST
inline void hostSetJsonArenaHooksInstalled(bool installed) {
  detail::g_jsonArenaHooksInstalled = installed;
}
#endif

inline void resetJsonArena() {
  detail::g_jsonArenaUsed = 0;
  detail::g_jsonArenaAllocFailures = 0;
}

inline size_t jsonArenaBytesUsed() {
  return detail::g_jsonArenaUsed;
}

inline uint32_t jsonArenaAllocFailures() {
  return detail::g_jsonArenaAllocFailures;
}

inline bool jsonArenaExhaustedRecently() {
  return detail::g_jsonArenaAllocFailures > 0 &&
         detail::g_jsonArenaUsed + 64 >= JSON_ARENA_CAPACITY;
}

}  // namespace shotstopper
