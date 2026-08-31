#pragma once

#include <stdint.h>

#include "ShotStopperBuzzerPatterns.h"
#include "ShotStopperRtttl.h"

namespace shotstopper {

inline bool buzzerPassiveBegin(uint8_t pin) {
  if (!ledcAttach(pin, BUZZER_TONE_HZ, 10)) {
    return false;
  }
  ledcWriteTone(pin, 0);
  return true;
}

inline void buzzerPassiveSetTone(uint8_t pin, uint32_t freqHz) {
  ledcWriteTone(pin, static_cast<double>(freqHz));
}

}  // namespace shotstopper
