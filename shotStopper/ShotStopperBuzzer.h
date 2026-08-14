#pragma once

#include <stdint.h>

#include "ShotStopperDomain.h"

namespace shotstopper {

enum class BuzzerPattern : uint8_t {
  NONE = 0,
  SINGLE = 1,
  TRIPLE = 2
};

constexpr uint32_t BUZZER_TONE_HZ = 2700;
constexpr uint32_t BUZZER_BEEP_ON_MS = 80;
constexpr uint32_t BUZZER_BEEP_GAP_MS = 70;
constexpr uint32_t BUZZER_SINGLE_ON_MS = 120;

// Non-blocking passive-piezo driver. Hardware PWM (ledc) on device; host tests
// stub tone as a digital level so patterns can be asserted without LEDC.
struct LocalBuzzer {
  uint8_t pin = 0;
  bool ready = false;
  BuzzerPattern active = BuzzerPattern::NONE;
  BuzzerPattern pending = BuzzerPattern::NONE;
  uint8_t beepIndex = 0;
  uint8_t beepCount = 0;
  bool toneOn = false;
  uint32_t phaseStartedAtMs = 0;
  uint32_t onMs = 0;
  uint32_t gapMs = 0;
  uint32_t acceptedRequests = 0;

  void begin(uint8_t gpioPin);
  // Starts immediately when idle, otherwise keeps one pending slot. TRIPLE
  // upgrades a pending SINGLE; a second SINGLE is rejected while TRIPLE is
  // pending. Returns false if unsupported, not ready, or not accepted.
  bool request(BuzzerPattern pattern);
  void service(uint32_t nowMs);
  bool busy() const {
    return active != BuzzerPattern::NONE || pending != BuzzerPattern::NONE;
  }

 private:
  void startTone();
  void stopTone();
  void finish(uint32_t nowMs);
  bool startPattern(BuzzerPattern pattern, uint32_t nowMs);
};

inline void LocalBuzzer::begin(uint8_t gpioPin) {
  pin = gpioPin;
  ready = false;
  active = BuzzerPattern::NONE;
  pending = BuzzerPattern::NONE;
  beepIndex = 0;
  beepCount = 0;
  toneOn = false;
  phaseStartedAtMs = 0;
  acceptedRequests = 0;
  if (!BUZZER_SUPPORT_ENABLED || pin == 0) {
    return;
  }
#if defined(SHOT_STOPPER_HOST_TEST)
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  ready = true;
#else
  // Arduino-ESP32 3.x API: attach pin then drive with writeTone.
  if (!ledcAttach(pin, BUZZER_TONE_HZ, 10)) {
    return;
  }
  ledcWriteTone(pin, 0);
  ready = true;
#endif
}

inline void LocalBuzzer::startTone() {
  if (!ready || toneOn) {
    return;
  }
#if defined(SHOT_STOPPER_HOST_TEST)
  digitalWrite(pin, HIGH);
#else
  ledcWriteTone(pin, BUZZER_TONE_HZ);
#endif
  toneOn = true;
}

inline void LocalBuzzer::stopTone() {
  if (!ready || !toneOn) {
    toneOn = false;
    return;
  }
#if defined(SHOT_STOPPER_HOST_TEST)
  digitalWrite(pin, LOW);
#else
  ledcWriteTone(pin, 0);
#endif
  toneOn = false;
}

inline void LocalBuzzer::finish(uint32_t nowMs) {
  stopTone();
  active = BuzzerPattern::NONE;
  beepIndex = 0;
  beepCount = 0;
  if (pending == BuzzerPattern::NONE) {
    return;
  }
  const BuzzerPattern next = pending;
  pending = BuzzerPattern::NONE;
  startPattern(next, nowMs);
}

inline bool LocalBuzzer::startPattern(BuzzerPattern pattern, uint32_t nowMs) {
  if (pattern == BuzzerPattern::SINGLE) {
    beepCount = 1;
    onMs = BUZZER_SINGLE_ON_MS;
    gapMs = 0;
  } else if (pattern == BuzzerPattern::TRIPLE) {
    beepCount = 3;
    onMs = BUZZER_BEEP_ON_MS;
    gapMs = BUZZER_BEEP_GAP_MS;
  } else {
    return false;
  }
  active = pattern;
  beepIndex = 0;
  phaseStartedAtMs = nowMs;
  startTone();
  return true;
}

inline bool LocalBuzzer::request(BuzzerPattern pattern) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || pattern == BuzzerPattern::NONE) {
    return false;
  }
  if (active == BuzzerPattern::NONE) {
    if (!startPattern(pattern, millis())) {
      return false;
    }
    ++acceptedRequests;
    return true;
  }
  if (pending == BuzzerPattern::NONE) {
    pending = pattern;
    ++acceptedRequests;
    return true;
  }
  if (pattern == BuzzerPattern::TRIPLE && pending == BuzzerPattern::SINGLE) {
    pending = BuzzerPattern::TRIPLE;
    return true;
  }
  // Same pattern already queued, or SINGLE while TRIPLE is pending.
  return pattern == pending;
}

inline void LocalBuzzer::service(uint32_t nowMs) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || active == BuzzerPattern::NONE) {
    return;
  }
  const uint32_t elapsed =
      static_cast<uint32_t>(nowMs - phaseStartedAtMs);
  if (toneOn) {
    if (elapsed < onMs) {
      return;
    }
    stopTone();
    phaseStartedAtMs = nowMs;
    if (beepIndex + 1U >= beepCount) {
      finish(nowMs);
    }
    return;
  }
  if (elapsed < gapMs) {
    return;
  }
  ++beepIndex;
  if (beepIndex >= beepCount) {
    finish(nowMs);
    return;
  }
  phaseStartedAtMs = nowMs;
  startTone();
}

}  // namespace shotstopper
