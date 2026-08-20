#pragma once

#include <cJSON.h>
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

constexpr size_t JSON_ARENA_CAPACITY = 8192;

inline void initJsonArenaHooks();
inline void resetJsonArena();
inline size_t jsonArenaBytesUsed();

namespace detail {

alignas(8) inline uint8_t g_jsonArena[JSON_ARENA_CAPACITY] = {};
inline size_t g_jsonArenaUsed = 0;
inline bool g_jsonArenaHooksInstalled = false;

inline void *jsonArenaMalloc(size_t size) {
  if (size == 0) {
    return nullptr;
  }
  const size_t aligned = (size + 7U) & ~size_t(7U);
  if (g_jsonArenaUsed + aligned > JSON_ARENA_CAPACITY) {
    return nullptr;
  }
  void *const block = g_jsonArena + g_jsonArenaUsed;
  g_jsonArenaUsed += aligned;
  return block;
}

inline void jsonArenaFree(void *) {}

}  // namespace detail

inline void initJsonArenaHooks() {
  if (detail::g_jsonArenaHooksInstalled) {
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
