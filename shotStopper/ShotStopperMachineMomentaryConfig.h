#pragma once

#include <string.h>

namespace shotstopper {

// =============================================================================
// SPECIALIZATION: Momentary — config helpers (API encoding)
// =============================================================================
// WHAT: JSON/API encoding for RuntimeConfig.momentaryStartOnPress.
//
// BOUNDARY: Momentary-only helpers. Paddle does not use this; keep it out of
// ShotStopperMachineTypes.h so brew/stopper never grow momentary-specific
// parsing paths.

inline const char *momentaryStartEdgeId(bool startOnPress) {
  return startOnPress ? "press" : "release";
}

inline bool parseMomentaryStartEdge(const char *text, bool &startOnPress) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "press") == 0) {
    startOnPress = true;
    return true;
  }
  if (strcmp(text, "release") == 0) {
    startOnPress = false;
    return true;
  }
  return false;
}

}  // namespace shotstopper
