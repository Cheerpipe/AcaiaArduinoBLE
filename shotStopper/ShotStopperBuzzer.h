#pragma once

#include <stddef.h>
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
constexpr uint32_t BUZZER_LONG_ON_MS = 800;
constexpr uint32_t BUZZER_RECOVERY_LONG_ON_MS = 1500;
constexpr uint32_t BUZZER_RECOVERY_PULSE_ON_MS = 50;
constexpr uint32_t BUZZER_RECOVERY_PULSE_GAP_MS = 50;
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

struct BuzzerNote {
  uint16_t onMs;
  uint16_t gapMs;
};

// Irregular finite motifs (not equal beeps, not a metronome).
constexpr BuzzerNote BUZZER_CHIME_NOTES[] = {{60, 50}, {60, 80}, {220, 0}};
constexpr BuzzerNote BUZZER_SWING_NOTES[] = {{180, 40}, {50, 40}, {50, 0}};
constexpr BuzzerNote BUZZER_ECHO_NOTES[] = {
    {50, 50}, {50, 220}, {50, 50}, {50, 0}};
// Echo inverted: long bookends with short middle ticks (trailing gap 0).
constexpr BuzzerNote BUZZER_ECHO_INVERTED_NOTES[] = {
    {220, 50}, {50, 50}, {50, 50}, {220, 0}};
constexpr BuzzerNote BUZZER_MORSE_NOTES[] = {{250, 80}, {50, 80}, {250, 0}};
constexpr BuzzerNote BUZZER_SNAP_NOTES[] = {
    {70, 30}, {200, 50}, {70, 30}, {200, 0}};
constexpr BuzzerNote BUZZER_RECOVERY_NETWORK_NOTES[] = {
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, 0}};
constexpr BuzzerNote BUZZER_RECOVERY_FACTORY_NOTES[] = {
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, BUZZER_RECOVERY_PULSE_GAP_MS},
    {BUZZER_RECOVERY_PULSE_ON_MS, 0}};
// A deliberately distinct long-short-long failure motif.
constexpr BuzzerNote BUZZER_RECOVERY_ERROR_NOTES[] = {
    {300, 100}, {70, 100}, {300, 0}};

constexpr bool buzzerNotesStartAndEndWithSound(const BuzzerNote *notes,
                                               size_t count) {
  if (notes == nullptr || count == 0) {
    return false;
  }
  if (notes[count - 1].gapMs != 0) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (notes[i].onMs == 0) {
      return false;
    }
  }
  return true;
}

template <size_t N>
constexpr bool buzzerSequenceStartsAndEndsWithSound(
    const BuzzerNote (&notes)[N]) {
  return buzzerNotesStartAndEndWithSound(notes, N);
}

static_assert(BUZZER_BEEP_ON_MS > 0 && BUZZER_SINGLE_ON_MS > 0 &&
                  BUZZER_LONG_ON_MS > 0 && BUZZER_RECOVERY_LONG_ON_MS > 0 &&
                  BUZZER_RECOVERY_PULSE_ON_MS > 0 &&
                  BUZZER_PULSE_TRAIN_ON_MS > 0,
              "fixed-length beeps must start with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_CHIME_NOTES),
              "CHIME must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_SWING_NOTES),
              "SWING must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_ECHO_NOTES),
              "ECHO must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_ECHO_INVERTED_NOTES),
              "ECHO_INVERTED must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_MORSE_NOTES),
              "MORSE must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_SNAP_NOTES),
              "SNAP must start and end with sound");
static_assert(
    buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_NETWORK_NOTES),
    "RECOVERY_NETWORK_OK must start and end with sound");
static_assert(
    buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_FACTORY_NOTES),
    "RECOVERY_FACTORY_OK must start and end with sound");
static_assert(buzzerSequenceStartsAndEndsWithSound(BUZZER_RECOVERY_ERROR_NOTES),
              "RECOVERY_ERROR must start and end with sound");

inline const BuzzerNote *buzzerSequenceNotes(BuzzerPattern pattern,
                                             uint8_t &count) {
  switch (pattern) {
    case BuzzerPattern::CHIME:
      count = static_cast<uint8_t>(sizeof(BUZZER_CHIME_NOTES) /
                                   sizeof(BUZZER_CHIME_NOTES[0]));
      return BUZZER_CHIME_NOTES;
    case BuzzerPattern::SWING:
      count = static_cast<uint8_t>(sizeof(BUZZER_SWING_NOTES) /
                                   sizeof(BUZZER_SWING_NOTES[0]));
      return BUZZER_SWING_NOTES;
    case BuzzerPattern::ECHO:
      count = static_cast<uint8_t>(sizeof(BUZZER_ECHO_NOTES) /
                                   sizeof(BUZZER_ECHO_NOTES[0]));
      return BUZZER_ECHO_NOTES;
    case BuzzerPattern::ECHO_INVERTED:
      count = static_cast<uint8_t>(sizeof(BUZZER_ECHO_INVERTED_NOTES) /
                                   sizeof(BUZZER_ECHO_INVERTED_NOTES[0]));
      return BUZZER_ECHO_INVERTED_NOTES;
    case BuzzerPattern::MORSE:
      count = static_cast<uint8_t>(sizeof(BUZZER_MORSE_NOTES) /
                                   sizeof(BUZZER_MORSE_NOTES[0]));
      return BUZZER_MORSE_NOTES;
    case BuzzerPattern::SNAP:
      count = static_cast<uint8_t>(sizeof(BUZZER_SNAP_NOTES) /
                                   sizeof(BUZZER_SNAP_NOTES[0]));
      return BUZZER_SNAP_NOTES;
    case BuzzerPattern::RECOVERY_NETWORK_OK:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_NETWORK_NOTES) /
                                   sizeof(BUZZER_RECOVERY_NETWORK_NOTES[0]));
      return BUZZER_RECOVERY_NETWORK_NOTES;
    case BuzzerPattern::RECOVERY_FACTORY_OK:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_FACTORY_NOTES) /
                                   sizeof(BUZZER_RECOVERY_FACTORY_NOTES[0]));
      return BUZZER_RECOVERY_FACTORY_NOTES;
    case BuzzerPattern::RECOVERY_ERROR:
      count = static_cast<uint8_t>(sizeof(BUZZER_RECOVERY_ERROR_NOTES) /
                                   sizeof(BUZZER_RECOVERY_ERROR_NOTES[0]));
      return BUZZER_RECOVERY_ERROR_NOTES;
    default:
      count = 0;
      return nullptr;
  }
}

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
  const BuzzerNote *sequenceNotes = nullptr;
  esp_timer_handle_t phaseTimer = nullptr;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  void begin(uint8_t gpioPin);
  // Starts immediately when idle, otherwise keeps one pending slot. High
  // priority patterns (TRIPLE, ECHO, ECHO_INVERTED) upgrade any weaker pending
  // pattern; a weaker pattern is rejected while a high-priority pattern is
  // pending. A finite pattern interrupts an active PULSE_TRAIN (and other
  // pulse rates) so a looping cue cannot block alarms. durationMs is used
  // only for pulse trains (0 = until stopIf). Returns false if unsupported,
  // not ready, or not accepted.
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
  void applySequenceNote(uint8_t index);
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
  sequenceNotes = nullptr;
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
  sequenceNotes = nullptr;
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

inline void LocalBuzzer::applySequenceNote(uint8_t index) {
  if (sequenceNotes == nullptr) {
    return;
  }
  onMs = sequenceNotes[index].onMs;
  gapMs = sequenceNotes[index].gapMs;
}

inline bool LocalBuzzer::startPattern(BuzzerPattern pattern, uint32_t nowMs) {
  looping = false;
  deadlineAtMs = 0;
  sequenceNotes = nullptr;
  if (pattern == BuzzerPattern::SINGLE) {
    beepCount = 1;
    onMs = BUZZER_SINGLE_ON_MS;
    gapMs = 0;
  } else if (pattern == BuzzerPattern::LONG) {
    beepCount = 1;
    onMs = BUZZER_LONG_ON_MS;
    gapMs = 0;
  } else if (pattern == BuzzerPattern::RECOVERY_LONG) {
    beepCount = 1;
    onMs = BUZZER_RECOVERY_LONG_ON_MS;
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
  } else if (buzzerPatternIsSequence(pattern)) {
    uint8_t count = 0;
    sequenceNotes = buzzerSequenceNotes(pattern, count);
    if (sequenceNotes == nullptr || count == 0) {
      return false;
    }
    beepCount = count;
    applySequenceNote(0);
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

inline bool buzzerPatternIsHighPriority(BuzzerPattern pattern) {
  return pattern == BuzzerPattern::TRIPLE || pattern == BuzzerPattern::ECHO ||
         pattern == BuzzerPattern::ECHO_INVERTED;
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
  } else if (buzzerPatternIsHighPriority(pattern) &&
             !buzzerPatternIsHighPriority(pending)) {
    pending = pattern;
    pendingDurationMs = buzzerPatternIsPulseTrain(pattern) ? durationMs : 0;
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
  sequenceNotes = nullptr;
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
  applySequenceNote(beepIndex);
  phaseStartedAtMs = nowMs;
  startTone();
  armPhaseTimer(onMs);
  portEXIT_CRITICAL(&mux);
}

}  // namespace shotstopper
