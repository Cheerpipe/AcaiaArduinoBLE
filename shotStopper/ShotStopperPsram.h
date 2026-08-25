#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <esp_attr.h>
#endif

// Place large BSS in PSRAM on the official IDF build
// (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y). Arduino-cli cores that ship
// that Kconfig off compile EXT_RAM_BSS_ATTR as empty, so the same objects stay
// in DRAM. Never mark flash DMA sources, OTA chunks, httpd bounce, or stacks of
// tasks that write flash: cache-off cannot reach PSRAM.
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#define SHOT_STOPPER_PSRAM_BSS
#else
#define SHOT_STOPPER_PSRAM_BSS EXT_RAM_BSS_ATTR
#endif

namespace shotstopper {

namespace detail {

inline uint32_t g_allocExternalOk = 0;
inline uint32_t g_allocExternalFallback = 0;
inline bool g_workBufExternal = false;

}  // namespace detail

inline uint32_t allocExternalOkCount() { return detail::g_allocExternalOk; }

inline uint32_t allocExternalFallbackCount() {
  return detail::g_allocExternalFallback;
}

inline bool workBufIsExternal() { return detail::g_workBufExternal; }

inline void noteWorkBufExternal(bool isExternal) {
  detail::g_workBufExternal = isExternal;
}

inline bool pointerIsExternal(const void *block) {
  if (block == nullptr) {
    return false;
  }
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  return true;
#else
  return esp_ptr_external_ram(block);
#endif
}

// SPIRAM only. Large blobs that must not punch a hole in internal DRAM
// (NetworkWorkBuf, Wi-Fi AP records). Returns nullptr if SPIRAM cannot
// satisfy — callers fail closed.
inline void *allocExternal(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) ||                                         \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  void *block = malloc(bytes);
  if (block != nullptr) {
    ++detail::g_allocExternalOk;
  }
  return block;
#else
  void *block = nullptr;
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
#endif
  if (block != nullptr && pointerIsExternal(block)) {
    ++detail::g_allocExternalOk;
    return block;
  }
  if (block != nullptr) {
    heap_caps_free(block);
  }
  return nullptr;
#endif
}

// Explicit caps: do not rely on malloc()>4KiB landing in PSRAM.
inline void *allocExternalOrInternal(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  void *block = malloc(bytes);
  if (block != nullptr) {
    ++detail::g_allocExternalOk;
  }
  return block;
#else
  void *block = nullptr;
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
#endif
  if (block != nullptr && pointerIsExternal(block)) {
    ++detail::g_allocExternalOk;
    return block;
  }
  if (block != nullptr) {
    heap_caps_free(block);
    block = nullptr;
  }
  block = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (block != nullptr) {
    ++detail::g_allocExternalFallback;
  }
  return block;
#endif
}

inline void *allocInternal(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  return malloc(bytes);
#else
  return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}

inline void heapCapsFree(void *block) {
  if (block == nullptr) {
    return;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  free(block);
#else
  heap_caps_free(block);
#endif
}

struct HeapCapSnapshot {
  uint32_t internalTotal = 0;
  uint32_t internalFree = 0;
  uint32_t internalMinimum = 0;
  uint32_t internalLargest = 0;
  uint32_t psramTotal = 0;
  uint32_t psramFree = 0;
  uint32_t psramLargest = 0;
};

inline HeapCapSnapshot sampleHeapCaps() {
  HeapCapSnapshot snap;
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  snap.internalTotal = 327680;
  snap.internalFree = 200000;
  snap.internalMinimum = 180000;
  snap.internalLargest = 100000;
#else
  const uint32_t internalCaps = MALLOC_CAP_INTERNAL;
  snap.internalTotal =
      static_cast<uint32_t>(heap_caps_get_total_size(internalCaps));
  snap.internalFree =
      static_cast<uint32_t>(heap_caps_get_free_size(internalCaps));
  snap.internalMinimum =
      static_cast<uint32_t>(heap_caps_get_minimum_free_size(internalCaps));
  snap.internalLargest = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(internalCaps));
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    const uint32_t psramCaps = MALLOC_CAP_SPIRAM;
    snap.psramTotal =
        static_cast<uint32_t>(heap_caps_get_total_size(psramCaps));
    snap.psramFree = static_cast<uint32_t>(heap_caps_get_free_size(psramCaps));
    snap.psramLargest = static_cast<uint32_t>(
        heap_caps_get_largest_free_block(psramCaps));
  }
#endif
#endif
  return snap;
}

}  // namespace shotstopper
