#pragma once

#include <stdint.h>

namespace shotstopper {

// Dual-slot pick by wrapping uint32 revision (settings + BLE Companion).
// Shot log uses an active-slot pointer instead; do not reuse this there.

enum class DualSlotChoice : uint8_t {
  NONE = 0,
  FIRST = 1,
  SECOND = 2,
};

inline bool secondRevisionIsNewer(uint32_t firstRevision,
                                  uint32_t secondRevision) {
  return static_cast<int32_t>(secondRevision - firstRevision) > 0;
}

inline DualSlotChoice chooseNewerRevision(bool firstValid,
                                          uint32_t firstRevision,
                                          bool secondValid,
                                          uint32_t secondRevision) {
  if (!firstValid && !secondValid) {
    return DualSlotChoice::NONE;
  }
  if (!firstValid) {
    return DualSlotChoice::SECOND;
  }
  if (!secondValid) {
    return DualSlotChoice::FIRST;
  }
  return secondRevisionIsNewer(firstRevision, secondRevision)
             ? DualSlotChoice::SECOND
             : DualSlotChoice::FIRST;
}

}  // namespace shotstopper
