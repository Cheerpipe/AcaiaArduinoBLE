#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_heap_caps.h>
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <esp_attr.h>
#endif

// Large, non-control-path BSS. Host tests and the BLE/relay/paddle path stay
// on internal SRAM.
#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) && defined(BOARD_HAS_PSRAM)
#define SHOT_STOPPER_PSRAM_BSS EXT_RAM_BSS_ATTR
#else
#define SHOT_STOPPER_PSRAM_BSS
#endif

namespace shotstopper {

// Explicit caps: do not rely on malloc()>4KiB landing in PSRAM.
inline void *allocExternalOrInternal(size_t bytes) {
  if (bytes == 0) {
    return nullptr;
  }
#if defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  return malloc(bytes);
#else
  void *block = nullptr;
#if defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
#endif
  if (block == nullptr) {
    block = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
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
