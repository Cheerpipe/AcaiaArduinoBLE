#pragma once

#include <stdint.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <esp_timer.h>
#endif

#include "ShotStopperDomain.h"

namespace shotstopper {

constexpr uint32_t BUZZER_TONE_HZ = 2700;
constexpr uint32_t BUZZER_BEEP_ON_MS = 80;
constexpr uint32_t BUZZER_BEEP_GAP_MS = 70;
constexpr uint32_t BUZZER_SINGLE_ON_MS = 120;
constexpr uint32_t BUZZER_LONG_ON_MS = 400;
constexpr uint32_t BUZZER_PULSE_TRAIN_ON_MS = 20;
constexpr uint32_t BUZZER_PULSE_TRAIN_PERIOD_MS = 500;
constexpr uint32_t BUZZER_PULSE_TRAIN_GAP_MS =
    BUZZER_PULSE_TRAIN_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_3HZ_PERIOD_MS = 333;
constexpr uint32_t BUZZER_PULSE_TRAIN_3HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_3HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_4HZ_PERIOD_MS = 250;
constexpr uint32_t BUZZER_PULSE_TRAIN_4HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_4HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_5HZ_PERIOD_MS = 200;
constexpr uint32_t BUZZER_PULSE_TRAIN_5HZ_GAP_MS =
    BUZZER_PULSE_TRAIN_5HZ_PERIOD_MS - BUZZER_PULSE_TRAIN_ON_MS;
constexpr uint32_t BUZZER_PULSE_TRAIN_DEBUG_MS = 3000;

// Non-blocking local-buzzer driver. Passive (ENABLE=1) uses hardware PWM
// (ledc); active (ENABLE=2) uses GPIO HIGH/LOW. Host stubs map both to a
// digital level so patterns can be asserted without LEDC. Phase edges are
// armed on esp_timer so Serial/loop stalls cannot stretch beeps.
struct LocalBuzzer {
  uint8_t pin = 0;
  bool ready = false;
  BuzzerPattern active = BuzzerPattern::NONE;
  BuzzerPattern pending = BuzzerPattern::NONE;
  uint8_t beepIndex = 0;
  uint8_t beepCount = 0;
  bool toneOn = false;
  bool looping = false;
  uint32_t phaseStartedAtMs = 0;
  uint32_t onMs = 0;
  uint32_t gapMs = 0;
  uint32_t deadlineAtMs = 0;
  uint32_t pendingDurationMs = 0;
  uint32_t acceptedRequests = 0;
  esp_timer_handle_t phaseTimer = nullptr;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  void begin(uint8_t gpioPin);
  // Starts immediately when idle, otherwise keeps one pending slot. TRIPLE
  // upgrades any non-TRIPLE pending pattern; a weaker pattern is rejected
  // while TRIPLE is pending. A finite pattern interrupts an active
  // PULSE_TRAIN (and other pulse rates) so a looping cue cannot block
  // alarms. durationMs is used only for pulse trains (0 = until stopIf).
  // Returns false if unsupported, not ready, or not accepted.
  bool request(BuzzerPattern pattern, uint32_t durationMs = 0);
  void stopIf(BuzzerPattern pattern);
  void service(uint32_t nowMs);
  bool busy() const {
    return active != BuzzerPattern::NONE || pending != BuzzerPattern::NONE;
  }

 private:
  void startTone();
  void stopTone();
  void applyDeadline(uint32_t durationMs, uint32_t nowMs);
  bool deadlineReached(uint32_t nowMs) const;
  void cancelPhaseTimer();
  void armPhaseTimer(uint32_t delayMs);
  void finish(uint32_t nowMs);
  bool startPattern(BuzzerPattern pattern, uint32_t nowMs);
  static void phaseTimerCallback(void *arg);
};

inline void LocalBuzzer::cancelPhaseTimer() {
  if (phaseTimer != nullptr) {
    (void)esp_timer_stop(phaseTimer);
  }
}

inline void LocalBuzzer::armPhaseTimer(uint32_t delayMs) {
  cancelPhaseTimer();
  if (phaseTimer == nullptr || delayMs == 0) {
    return;
  }
  (void)esp_timer_start_once(phaseTimer,
                             static_cast<uint64_t>(delayMs) * 1000ULL);
}

inline void LocalBuzzer::phaseTimerCallback(void *arg) {
  if (arg == nullptr) {
    return;
  }
  static_cast<LocalBuzzer *>(arg)->service(millis());
}

inline void LocalBuzzer::begin(uint8_t gpioPin) {
  pin = gpioPin;
  ready = false;
  active = BuzzerPattern::NONE;
  pending = BuzzerPattern::NONE;
  beepIndex = 0;
  beepCount = 0;
  toneOn = false;
  looping = false;
  phaseStartedAtMs = 0;
  onMs = 0;
  gapMs = 0;
  deadlineAtMs = 0;
  pendingDurationMs = 0;
  acceptedRequests = 0;
  cancelPhaseTimer();
  if (!BUZZER_SUPPORT_ENABLED || pin == 0) {
    return;
  }
  if (phaseTimer == nullptr) {
    esp_timer_create_args_t args = {};
    args.callback = &LocalBuzzer::phaseTimerCallback;
    args.arg = this;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "buzzer_phase";
    if (esp_timer_create(&args, &phaseTimer) != ESP_OK) {
      phaseTimer = nullptr;
    }
  }
  if (BUZZER_ACTIVE_DRIVE) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    ready = true;
    return;
  }
  // Passive piezo: Arduino-ESP32 3.x API attach then writeTone.
  if (!ledcAttach(pin, BUZZER_TONE_HZ, 10)) {
    return;
  }
  ledcWriteTone(pin, 0);
  ready = true;
}

inline void LocalBuzzer::startTone() {
  if (!ready || toneOn) {
    return;
  }
  if (BUZZER_ACTIVE_DRIVE) {
    digitalWrite(pin, HIGH);
  } else {
    ledcWriteTone(pin, BUZZER_TONE_HZ);
  }
  toneOn = true;
}

inline void LocalBuzzer::stopTone() {
  if (!ready || !toneOn) {
    toneOn = false;
    return;
  }
  if (BUZZER_ACTIVE_DRIVE) {
    digitalWrite(pin, LOW);
  } else {
    ledcWriteTone(pin, 0);
  }
  toneOn = false;
}

inline void LocalBuzzer::applyDeadline(uint32_t durationMs, uint32_t nowMs) {
  deadlineAtMs = durationMs == 0 ? 0 : nowMs + durationMs;
}

inline bool LocalBuzzer::deadlineReached(uint32_t nowMs) const {
  return deadlineAtMs != 0 &&
         static_cast<int32_t>(nowMs - deadlineAtMs) >= 0;
}

inline void LocalBuzzer::finish(uint32_t nowMs) {
  stopTone();
  active = BuzzerPattern::NONE;
  beepIndex = 0;
  beepCount = 0;
  looping = false;
  deadlineAtMs = 0;
  if (pending == BuzzerPattern::NONE) {
    cancelPhaseTimer();
    return;
  }
  const BuzzerPattern next = pending;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingDurationMs = 0;
  if (!startPattern(next, nowMs)) {
    cancelPhaseTimer();
    return;
  }
  applyDeadline(nextDurationMs, nowMs);
}

inline bool LocalBuzzer::startPattern(BuzzerPattern pattern, uint32_t nowMs) {
  looping = false;
  deadlineAtMs = 0;
  if (pattern == BuzzerPattern::SINGLE) {
    beepCount = 1;
    onMs = BUZZER_SINGLE_ON_MS;
    gapMs = 0;
  } else if (pattern == BuzzerPattern::LONG) {
    beepCount = 1;
    onMs = BUZZER_LONG_ON_MS;
    gapMs = 0;
  } else if (pattern == BuzzerPattern::DOUBLE) {
    beepCount = 2;
    onMs = BUZZER_BEEP_ON_MS;
    gapMs = BUZZER_BEEP_GAP_MS;
  } else if (pattern == BuzzerPattern::TRIPLE) {
    beepCount = 3;
    onMs = BUZZER_BEEP_ON_MS;
    gapMs = BUZZER_BEEP_GAP_MS;
  } else if (buzzerPatternIsPulseTrain(pattern)) {
    beepCount = 0;
    looping = true;
    onMs = BUZZER_PULSE_TRAIN_ON_MS;
    if (pattern == BuzzerPattern::PULSE_3HZ) {
      gapMs = BUZZER_PULSE_TRAIN_3HZ_GAP_MS;
    } else if (pattern == BuzzerPattern::PULSE_4HZ) {
      gapMs = BUZZER_PULSE_TRAIN_4HZ_GAP_MS;
    } else if (pattern == BuzzerPattern::PULSE_5HZ) {
      gapMs = BUZZER_PULSE_TRAIN_5HZ_GAP_MS;
    } else {
      gapMs = BUZZER_PULSE_TRAIN_GAP_MS;
    }
  } else {
    return false;
  }
  active = pattern;
  beepIndex = 0;
  phaseStartedAtMs = nowMs;
  startTone();
  armPhaseTimer(onMs);
  return true;
}

inline bool LocalBuzzer::request(BuzzerPattern pattern, uint32_t durationMs) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || pattern == BuzzerPattern::NONE) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  const uint32_t nowMs = millis();
  bool accepted = false;
  if (buzzerPatternIsPulseTrain(active)) {
    if (pattern == active) {
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    } else {
      pending = BuzzerPattern::NONE;
      pendingDurationMs = 0;
      if (startPattern(pattern, nowMs)) {
        applyDeadline(durationMs, nowMs);
        ++acceptedRequests;
        accepted = true;
      }
    }
  } else if (active == BuzzerPattern::NONE) {
    if (startPattern(pattern, nowMs)) {
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    }
  } else if (pending == BuzzerPattern::NONE) {
    pending = pattern;
    pendingDurationMs = durationMs;
    ++acceptedRequests;
    accepted = true;
  } else if (pattern == BuzzerPattern::TRIPLE &&
             pending != BuzzerPattern::TRIPLE) {
    pending = BuzzerPattern::TRIPLE;
    pendingDurationMs = 0;
    accepted = true;
  } else {
    accepted = pattern == pending;
  }
  portEXIT_CRITICAL(&mux);
  return accepted;
}

inline void LocalBuzzer::stopIf(BuzzerPattern pattern) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || pattern == BuzzerPattern::NONE) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (pending == pattern) {
    pending = BuzzerPattern::NONE;
    pendingDurationMs = 0;
  }
  if (active != pattern) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  const uint32_t nowMs = millis();
  stopTone();
  active = BuzzerPattern::NONE;
  beepIndex = 0;
  beepCount = 0;
  looping = false;
  deadlineAtMs = 0;
  if (pending == BuzzerPattern::NONE) {
    cancelPhaseTimer();
    portEXIT_CRITICAL(&mux);
    return;
  }
  const BuzzerPattern next = pending;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingDurationMs = 0;
  if (!startPattern(next, nowMs)) {
    cancelPhaseTimer();
    portEXIT_CRITICAL(&mux);
    return;
  }
  applyDeadline(nextDurationMs, nowMs);
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::service(uint32_t nowMs) {
  if (!BUZZER_SUPPORT_ENABLED || !ready) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (active == BuzzerPattern::NONE) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (looping && deadlineReached(nowMs)) {
    finish(nowMs);
    portEXIT_CRITICAL(&mux);
    return;
  }
  const uint32_t elapsed =
      static_cast<uint32_t>(nowMs - phaseStartedAtMs);
  if (toneOn) {
    if (elapsed < onMs) {
      portEXIT_CRITICAL(&mux);
      return;
    }
    stopTone();
    phaseStartedAtMs = nowMs;
    if (looping) {
      if (deadlineReached(nowMs)) {
        finish(nowMs);
      } else {
        armPhaseTimer(gapMs);
      }
      portEXIT_CRITICAL(&mux);
      return;
    }
    if (beepIndex + 1U >= beepCount) {
      finish(nowMs);
    } else {
      armPhaseTimer(gapMs);
    }
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (elapsed < gapMs) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (looping) {
    if (deadlineReached(nowMs)) {
      finish(nowMs);
      portEXIT_CRITICAL(&mux);
      return;
    }
    phaseStartedAtMs = nowMs;
    startTone();
    armPhaseTimer(onMs);
    portEXIT_CRITICAL(&mux);
    return;
  }
  ++beepIndex;
  if (beepIndex >= beepCount) {
    finish(nowMs);
    portEXIT_CRITICAL(&mux);
    return;
  }
  phaseStartedAtMs = nowMs;
  startTone();
  armPhaseTimer(onMs);
  portEXIT_CRITICAL(&mux);
}

}  // namespace shotstopper
