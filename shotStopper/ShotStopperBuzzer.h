#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <esp_timer.h>
#endif

#include "ShotStopperAlertTone.h"
#include "ShotStopperBuzzerActive.h"
#include "ShotStopperBuzzerPassive.h"
#include "ShotStopperDomain.h"

namespace shotstopper {

inline bool buzzerPatternIsHighPriority(BuzzerPattern pattern) {
  return pattern == BuzzerPattern::TRIPLE || pattern == BuzzerPattern::ECHO ||
         pattern == BuzzerPattern::ECHO_INVERTED;
}

// Non-blocking local-buzzer driver. Passive (ENABLE=1) uses hardware PWM
// (ledc) and RTTTL for operational cues; active (ENABLE=2) uses GPIO
// HIGH/LOW. Host stubs map both to a digital level. Phase edges are armed
// on esp_timer so Serial/loop stalls cannot stretch beeps.
struct LocalBuzzer {
  uint8_t pin = 0;
  bool ready = false;
  BuzzerPattern active = BuzzerPattern::NONE;
  BuzzerPattern pending = BuzzerPattern::NONE;
  BuzzerCue activeCue = BuzzerCue::NONE;
  BuzzerCue pendingCue = BuzzerCue::NONE;
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
  uint32_t toneHz = BUZZER_TONE_HZ;
  const BuzzerNote *sequenceNotes = nullptr;
  RtttlNote rtttlBuf[RTTTL_MAX_NOTES] = {};
  uint8_t rtttlCount = 0;
  bool rtttlPlayback = false;
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  static constexpr uint8_t kRtttlCueCount =
      static_cast<uint8_t>(BuzzerCue::RECOVERY_ERROR) + 1;
  RtttlNote cueNotes[kRtttlCueCount][RTTTL_MAX_NOTES] = {};
  uint8_t cueNoteCount[kRtttlCueCount] = {};
#endif
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
  bool requestTone(const BuzzerToneCommand &cmd);
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
  bool startRtttl(const BuzzerToneCommand &cmd, uint32_t nowMs);
  void applySequenceNote(uint8_t index);
  void applyRtttlNote(uint8_t index);
  void clearPlayback();
  bool acceptLocked(BuzzerPattern pattern, BuzzerCue cue, uint32_t durationMs,
                    bool rtttl, const BuzzerToneCommand *cmd);
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

inline void LocalBuzzer::clearPlayback() {
  active = BuzzerPattern::NONE;
  activeCue = BuzzerCue::NONE;
  beepIndex = 0;
  beepCount = 0;
  looping = false;
  deadlineAtMs = 0;
  sequenceNotes = nullptr;
  rtttlCount = 0;
  rtttlPlayback = false;
  toneHz = BUZZER_TONE_HZ;
}

inline void LocalBuzzer::begin(uint8_t gpioPin) {
  pin = gpioPin;
  ready = false;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingDurationMs = 0;
  acceptedRequests = 0;
  toneOn = false;
  clearPlayback();
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
    buzzerActiveBegin(pin);
    ready = true;
    return;
  }
  if (!buzzerPassiveBegin(pin)) {
    return;
  }
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  for (uint8_t i = 1; i < kRtttlCueCount; ++i) {
    uint8_t count = 0;
    if (!parseRtttl(rtttlForCue(static_cast<BuzzerCue>(i)), cueNotes[i],
                    count) ||
        count == 0) {
      return;
    }
    cueNoteCount[i] = count;
  }
#endif
  ready = true;
}

inline void LocalBuzzer::startTone() {
  if (!ready || toneOn || toneHz == 0) {
    return;
  }
  if (BUZZER_ACTIVE_DRIVE) {
    buzzerActiveSetTone(pin, true);
  } else {
    buzzerPassiveSetTone(pin, toneHz);
  }
  toneOn = true;
}

inline void LocalBuzzer::stopTone() {
  if (!ready || !toneOn) {
    toneOn = false;
    return;
  }
  if (BUZZER_ACTIVE_DRIVE) {
    buzzerActiveSetTone(pin, false);
  } else {
    buzzerPassiveSetTone(pin, 0);
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
  clearPlayback();
  if (pending == BuzzerPattern::NONE) {
    cancelPhaseTimer();
    return;
  }
  const BuzzerPattern next = pending;
  const BuzzerCue nextCue = pendingCue;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingDurationMs = 0;
  bool started = false;
  if (nextCue != BuzzerCue::NONE && !BUZZER_ACTIVE_DRIVE) {
    BuzzerToneCommand cmd = deriveRecoveryTone(nextCue);
    cmd.pattern = next;
    cmd.looping = buzzerCueIsLooping(nextCue) || buzzerPatternIsPulseTrain(next);
    cmd.durationMs = nextDurationMs;
    started = startRtttl(cmd, nowMs);
  } else {
    started = startPattern(next, nowMs);
    if (started) {
      activeCue = nextCue;
    }
  }
  if (!started) {
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
  toneHz = BUZZER_TONE_HZ;
}

inline void LocalBuzzer::applyRtttlNote(uint8_t index) {
  if (index >= rtttlCount) {
    return;
  }
  toneHz = rtttlBuf[index].freqHz;
  if (toneHz == 0) {
    onMs = 0;
    gapMs = rtttlBuf[index].durationMs;
  } else {
    onMs = rtttlBuf[index].durationMs;
    gapMs = 0;
  }
}

inline bool LocalBuzzer::startRtttl(const BuzzerToneCommand &cmd,
                                    uint32_t nowMs) {
#if SHOT_STOPPER_ENABLE_BUZZER != 1
  (void)cmd;
  (void)nowMs;
  return false;
#else
  const uint8_t id = static_cast<uint8_t>(cmd.cue);
  if (id == 0 || id >= kRtttlCueCount || cueNoteCount[id] == 0) {
    return false;
  }
  rtttlCount = cueNoteCount[id];
  memcpy(rtttlBuf, cueNotes[id], sizeof(RtttlNote) * rtttlCount);
  rtttlPlayback = true;
  sequenceNotes = nullptr;
  looping = cmd.looping;
  deadlineAtMs = 0;
  active = cmd.pattern;
  activeCue = cmd.cue;
  beepCount = rtttlCount;
  beepIndex = 0;
  applyRtttlNote(0);
  phaseStartedAtMs = nowMs;
  if (toneHz > 0) {
    startTone();
    armPhaseTimer(onMs);
  } else {
    armPhaseTimer(gapMs);
  }
  return true;
#endif
}

inline bool LocalBuzzer::startPattern(BuzzerPattern pattern, uint32_t nowMs) {
  looping = false;
  deadlineAtMs = 0;
  sequenceNotes = nullptr;
  rtttlPlayback = false;
  rtttlCount = 0;
  activeCue = BuzzerCue::NONE;
  toneHz = BUZZER_TONE_HZ;
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

inline bool LocalBuzzer::acceptLocked(BuzzerPattern pattern, BuzzerCue cue,
                                      uint32_t durationMs, bool rtttl,
                                      const BuzzerToneCommand *cmd) {
  const uint32_t nowMs = millis();
  bool started = false;
  bool accepted = false;
  if (buzzerPatternIsPulseTrain(active) ||
      (active != BuzzerPattern::NONE && looping && rtttlPlayback)) {
    if (pattern == active && cue == activeCue) {
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    } else {
      pending = BuzzerPattern::NONE;
      pendingCue = BuzzerCue::NONE;
      pendingDurationMs = 0;
      stopTone();
      if (rtttl && cmd != nullptr) {
        started = startRtttl(*cmd, nowMs);
      } else {
        started = startPattern(pattern, nowMs);
      }
      if (started) {
        if (!rtttl) {
          activeCue = cue;
        }
        applyDeadline(durationMs, nowMs);
        ++acceptedRequests;
        accepted = true;
      }
    }
  } else if (active == BuzzerPattern::NONE) {
    if (rtttl && cmd != nullptr) {
      started = startRtttl(*cmd, nowMs);
    } else {
      started = startPattern(pattern, nowMs);
    }
    if (started) {
      if (!rtttl) {
        activeCue = cue;
      }
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    }
  } else if (pending == BuzzerPattern::NONE) {
    pending = pattern;
    pendingCue = cue;
    pendingDurationMs = durationMs;
    ++acceptedRequests;
    accepted = true;
  } else if (buzzerPatternIsHighPriority(pattern) &&
             !buzzerPatternIsHighPriority(pending)) {
    pending = pattern;
    pendingCue = cue;
    pendingDurationMs = buzzerPatternIsPulseTrain(pattern) ? durationMs : 0;
    accepted = true;
  } else {
    accepted = pattern == pending && cue == pendingCue;
  }
  return accepted;
}

inline bool LocalBuzzer::request(BuzzerPattern pattern, uint32_t durationMs) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || pattern == BuzzerPattern::NONE) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  const bool accepted =
      acceptLocked(pattern, BuzzerCue::NONE, durationMs, false, nullptr);
  portEXIT_CRITICAL(&mux);
  return accepted;
}

inline bool LocalBuzzer::requestTone(const BuzzerToneCommand &cmd) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || !cmd.valid ||
      cmd.pattern == BuzzerPattern::NONE) {
    return false;
  }
  const bool useRtttl = !BUZZER_ACTIVE_DRIVE && cmd.cue != BuzzerCue::NONE &&
                        rtttlForCue(cmd.cue) != nullptr;
  portENTER_CRITICAL(&mux);
  const bool accepted =
      acceptLocked(cmd.pattern, cmd.cue, cmd.durationMs, useRtttl, &cmd);
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
    pendingCue = BuzzerCue::NONE;
    pendingDurationMs = 0;
  }
  if (active != pattern) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  const uint32_t nowMs = millis();
  stopTone();
  clearPlayback();
  if (pending == BuzzerPattern::NONE) {
    cancelPhaseTimer();
    portEXIT_CRITICAL(&mux);
    return;
  }
  const BuzzerPattern next = pending;
  const BuzzerCue nextCue = pendingCue;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingDurationMs = 0;
  bool started = false;
  if (nextCue != BuzzerCue::NONE && !BUZZER_ACTIVE_DRIVE) {
    BuzzerToneCommand cmd = deriveRecoveryTone(nextCue);
    cmd.pattern = next;
    cmd.looping = buzzerCueIsLooping(nextCue) || buzzerPatternIsPulseTrain(next);
    cmd.durationMs = nextDurationMs;
    started = startRtttl(cmd, nowMs);
  } else {
    started = startPattern(next, nowMs);
    if (started) {
      activeCue = nextCue;
    }
  }
  if (!started) {
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
    if (looping && !rtttlPlayback) {
      if (deadlineReached(nowMs)) {
        finish(nowMs);
      } else {
        armPhaseTimer(gapMs);
      }
      portEXIT_CRITICAL(&mux);
      return;
    }
    if (beepIndex + 1U >= beepCount) {
      if (looping && rtttlPlayback && !deadlineReached(nowMs)) {
        beepIndex = 0;
        applyRtttlNote(0);
        if (gapMs == 0 && toneHz > 0) {
          startTone();
          armPhaseTimer(onMs);
        } else {
          armPhaseTimer(gapMs == 0 ? 1 : gapMs);
        }
      } else {
        finish(nowMs);
      }
    } else if (gapMs == 0 && rtttlPlayback) {
      ++beepIndex;
      applyRtttlNote(beepIndex);
      if (toneHz > 0) {
        startTone();
        armPhaseTimer(onMs);
      } else {
        armPhaseTimer(gapMs == 0 ? 1 : gapMs);
      }
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
  if (looping && !rtttlPlayback) {
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
  if (rtttlPlayback && toneHz == 0 && beepIndex + 1U >= beepCount) {
    if (looping && !deadlineReached(nowMs)) {
      beepIndex = 0;
      applyRtttlNote(0);
      phaseStartedAtMs = nowMs;
      if (toneHz > 0) {
        startTone();
        armPhaseTimer(onMs);
      } else {
        armPhaseTimer(gapMs == 0 ? 1 : gapMs);
      }
      portEXIT_CRITICAL(&mux);
      return;
    }
    finish(nowMs);
    portEXIT_CRITICAL(&mux);
    return;
  }
  ++beepIndex;
  if (beepIndex >= beepCount) {
    finish(nowMs);
    portEXIT_CRITICAL(&mux);
    return;
  }
  if (rtttlPlayback) {
    applyRtttlNote(beepIndex);
  } else {
    applySequenceNote(beepIndex);
  }
  phaseStartedAtMs = nowMs;
  if (toneHz > 0) {
    startTone();
    armPhaseTimer(onMs);
  } else {
    armPhaseTimer(gapMs == 0 ? 1 : gapMs);
  }
  portEXIT_CRITICAL(&mux);
}

}  // namespace shotstopper
