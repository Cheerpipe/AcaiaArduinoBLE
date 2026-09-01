#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <esp_timer.h>
#endif

#include "ShotStopperAlertTone.h"
#include "ShotStopperBullseye.h"
#include "ShotStopperBuzzerPatterns.h"
#include "ShotStopperBuzzerPassive.h"
#include "ShotStopperDomain.h"

namespace shotstopper {

inline bool buzzerPatternIsHighPriority(BuzzerPattern pattern) {
  return pattern == BuzzerPattern::TRIPLE || pattern == BuzzerPattern::ECHO ||
         pattern == BuzzerPattern::ECHO_INVERTED;
}

inline bool buzzerRequestIsHighPriority(BuzzerPattern pattern, BuzzerCue cue) {
  if (cue != BuzzerCue::NONE) {
    return buzzerCueIsHighPriority(cue);
  }
  return buzzerPatternIsHighPriority(pattern);
}

// Non-blocking local-buzzer driver. Passive piezo via hardware PWM (ledc)
// and RTTTL for operational cues. Host stubs map PWM to a digital level.
// Phase edges are armed on esp_timer so Serial/loop stalls cannot stretch
// beeps. Debug/BUZZER_TEST still plays timed BuzzerPattern motifs at
// BUZZER_TONE_HZ.
struct LocalBuzzer {
  uint8_t pin = 0;
  bool ready = false;
  BuzzerPattern active = BuzzerPattern::NONE;
  BuzzerPattern pending = BuzzerPattern::NONE;
  BuzzerCue activeCue = BuzzerCue::NONE;
  BuzzerCue pendingCue = BuzzerCue::NONE;
  uint8_t activePulseRate = 0;
  uint8_t pendingPulseRate = 0;
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
  // Playback and custom-note buffers stay in internal RAM because esp_timer
  // can advance a queued tune while flash writes make PSRAM inaccessible.
  // The custom tune is parsed only when settings change; starting it is a
  // bounded memcpy with no allocation or expensive parsing under the mux.
  RtttlNote rtttlBuf[BULLSEYE_RTTTL_MAX_NOTES] = {};
  RtttlNote bullseyeNotes[BULLSEYE_RTTTL_MAX_NOTES] = {};
  uint8_t rtttlCount = 0;
  uint8_t bullseyeNoteCount = 0;
  bool rtttlPlayback = false;
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  static constexpr uint8_t kRtttlCueCount =
      static_cast<uint8_t>(BuzzerCue::RECOVERY_ERROR) + 1;
  static constexpr uint8_t kPulseRateCount =
      static_cast<uint8_t>(ExtendedPulseRate::RAPID) + 1;
  RtttlNote cueNotes[kRtttlCueCount][RTTTL_MAX_NOTES] = {};
  uint8_t cueNoteCount[kRtttlCueCount] = {};
  RtttlNote pulseNotes[kPulseRateCount][RTTTL_MAX_NOTES] = {};
  uint8_t pulseNoteCount[kPulseRateCount] = {};
#endif
  esp_timer_handle_t phaseTimer = nullptr;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  void begin(uint8_t gpioPin);
  // Starts immediately when idle, otherwise keeps one pending slot. High
  // priority patterns (TRIPLE, ECHO, ECHO_INVERTED) and cues (NO_SCALE,
  // GUARD_STOP, SCALE_CONNECTED, SCALE_LOST) upgrade any weaker pending
  // slot; a weaker request is rejected while a high-priority slot is
  // pending. A finite pattern interrupts an active looping pulse so a
  // looping cue cannot block alarms. durationMs is used only for pulse
  // trains (0 = until stopIf). Returns false if unsupported, not ready,
  // or not accepted.
  bool request(BuzzerPattern pattern, uint32_t durationMs = 0);
  bool requestTone(const BuzzerToneCommand &cmd);
  bool configureBullseyeRtttl(const char *rtttl);
  bool requestBullseye();
  void stopIf(BuzzerPattern pattern);
  void stopIfCue(BuzzerCue cue);
  void stopAll();
  // Drop a queued extended pulse and an infinite operational/debug pulse.
  // Finite Debug Slow/Medium/Fast/Rapid (deadline set) keep playing.
  void stopExtendedPulse();
  // End-of-cycle: drop queued and active pulse trains / looping cues.
  void stopPulseTrains();
  void service(uint32_t nowMs);
  bool busy() const {
    return active != BuzzerPattern::NONE || pending != BuzzerPattern::NONE ||
           activeCue != BuzzerCue::NONE || pendingCue != BuzzerCue::NONE;
  }
  bool playingExtendedPulse(const BuzzerToneCommand &cmd) const {
    return cmd.valid &&
           ((activeCue == cmd.cue && activePulseRate == cmd.pulseRate) ||
            (pendingCue == cmd.cue && pendingPulseRate == cmd.pulseRate));
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
  bool startPendingLocked(BuzzerPattern pattern, BuzzerCue cue,
                          uint8_t pulseRate, uint32_t durationMs,
                          uint32_t nowMs);
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
  activePulseRate = 0;
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
  pendingPulseRate = 0;
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
  if (!buzzerPassiveBegin(pin)) {
    return;
  }
#if SHOT_STOPPER_ENABLE_BUZZER == 1
  for (uint8_t i = 1; i < kRtttlCueCount; ++i) {
    const BuzzerCue cue = static_cast<BuzzerCue>(i);
    if (buzzerCueIsLooping(cue) || cue == BuzzerCue::BULLSEYE) {
      continue;
    }
    uint8_t count = 0;
    if (!parseRtttl(rtttlForCue(cue), cueNotes[i], count) || count == 0) {
      return;
    }
    cueNoteCount[i] = count;
  }
  for (uint8_t rate = 1; rate < kPulseRateCount; ++rate) {
    uint8_t count = 0;
    if (!parseRtttl(rtttlForExtendedPulseRate(rate), pulseNotes[rate], count) ||
        count == 0) {
      return;
    }
    pulseNoteCount[rate] = count;
  }
#endif
  ready = true;
}

inline void LocalBuzzer::startTone() {
  if (!ready || toneOn || toneHz == 0) {
    return;
  }
  buzzerPassiveSetTone(pin, toneHz);
  toneOn = true;
}

inline void LocalBuzzer::stopTone() {
  if (!ready || !toneOn) {
    toneOn = false;
    return;
  }
  buzzerPassiveSetTone(pin, 0);
  toneOn = false;
}

inline void LocalBuzzer::applyDeadline(uint32_t durationMs, uint32_t nowMs) {
  deadlineAtMs = durationMs == 0 ? 0 : nowMs + durationMs;
}

inline bool LocalBuzzer::deadlineReached(uint32_t nowMs) const {
  return deadlineAtMs != 0 &&
         static_cast<int32_t>(nowMs - deadlineAtMs) >= 0;
}

inline bool LocalBuzzer::startPendingLocked(BuzzerPattern pattern, BuzzerCue cue,
                                            uint8_t pulseRate,
                                            uint32_t durationMs,
                                            uint32_t nowMs) {
  bool started = false;
  if (cue != BuzzerCue::NONE) {
    BuzzerToneCommand cmd;
    cmd.cue = cue;
    cmd.pulseRate = pulseRate;
    cmd.looping = buzzerCueIsLooping(cue);
    cmd.durationMs = durationMs;
    cmd.valid = true;
    started = startRtttl(cmd, nowMs);
  } else {
    started = startPattern(pattern, nowMs);
  }
  if (started) {
    applyDeadline(durationMs, nowMs);
  }
  return started;
}

inline void LocalBuzzer::finish(uint32_t nowMs) {
  stopTone();
  clearPlayback();
  if (pending == BuzzerPattern::NONE && pendingCue == BuzzerCue::NONE) {
    cancelPhaseTimer();
    return;
  }
  const BuzzerPattern next = pending;
  const BuzzerCue nextCue = pendingCue;
  const uint8_t nextPulseRate = pendingPulseRate;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingPulseRate = 0;
  pendingDurationMs = 0;
  if (!startPendingLocked(next, nextCue, nextPulseRate, nextDurationMs,
                          nowMs)) {
    cancelPhaseTimer();
  }
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
  const bool pulse = buzzerCueIsLooping(cmd.cue);
  if (cmd.cue == BuzzerCue::BULLSEYE) {
    if (bullseyeNoteCount == 0) {
      return false;
    }
    rtttlCount = bullseyeNoteCount;
    memcpy(rtttlBuf, bullseyeNotes, sizeof(RtttlNote) * rtttlCount);
    activePulseRate = 0;
  } else if (pulse) {
    const uint8_t rate = cmd.pulseRate;
    if (rate == 0 || rate >= kPulseRateCount || pulseNoteCount[rate] == 0) {
      return false;
    }
    rtttlCount = pulseNoteCount[rate];
    memcpy(rtttlBuf, pulseNotes[rate], sizeof(RtttlNote) * rtttlCount);
    activePulseRate = rate;
  } else {
    if (id == 0 || id >= kRtttlCueCount || cueNoteCount[id] == 0) {
      return false;
    }
    rtttlCount = cueNoteCount[id];
    memcpy(rtttlBuf, cueNotes[id], sizeof(RtttlNote) * rtttlCount);
    activePulseRate = 0;
  }
  rtttlPlayback = true;
  sequenceNotes = nullptr;
  looping = cmd.looping;
  deadlineAtMs = 0;
  active = BuzzerPattern::NONE;
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
  activePulseRate = 0;
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
  const uint8_t pulseRate = cmd != nullptr ? cmd->pulseRate : 0;
  bool started = false;
  bool accepted = false;
  if (buzzerPatternIsPulseTrain(active) || (looping && rtttlPlayback)) {
    if (pattern == active && cue == activeCue &&
        pulseRate == activePulseRate) {
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    } else {
      pending = BuzzerPattern::NONE;
      pendingCue = BuzzerCue::NONE;
      pendingPulseRate = 0;
      pendingDurationMs = 0;
      stopTone();
      if (rtttl && cmd != nullptr) {
        started = startRtttl(*cmd, nowMs);
      } else {
        started = startPattern(pattern, nowMs);
      }
      if (started) {
        applyDeadline(durationMs, nowMs);
        ++acceptedRequests;
        accepted = true;
      }
    }
  } else if (active == BuzzerPattern::NONE && activeCue == BuzzerCue::NONE) {
    if (rtttl && cmd != nullptr) {
      started = startRtttl(*cmd, nowMs);
    } else {
      started = startPattern(pattern, nowMs);
    }
    if (started) {
      applyDeadline(durationMs, nowMs);
      ++acceptedRequests;
      accepted = true;
    }
  } else if (pending == BuzzerPattern::NONE && pendingCue == BuzzerCue::NONE) {
    pending = pattern;
    pendingCue = cue;
    pendingPulseRate = pulseRate;
    pendingDurationMs = durationMs;
    ++acceptedRequests;
    accepted = true;
  } else if (buzzerRequestIsHighPriority(pattern, cue) &&
             !buzzerRequestIsHighPriority(pending, pendingCue)) {
    pending = pattern;
    pendingCue = cue;
    pendingPulseRate = pulseRate;
    pendingDurationMs =
        buzzerPatternIsPulseTrain(pattern) || buzzerCueIsLooping(cue)
            ? durationMs
            : 0;
    accepted = true;
  } else {
    accepted = pattern == pending && cue == pendingCue &&
               pulseRate == pendingPulseRate;
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
      cmd.cue == BuzzerCue::NONE) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  const bool accepted =
      acceptLocked(BuzzerPattern::NONE, cmd.cue, cmd.durationMs, true, &cmd);
  portEXIT_CRITICAL(&mux);
  return accepted;
}

inline bool LocalBuzzer::configureBullseyeRtttl(const char *rtttl) {
#if SHOT_STOPPER_ENABLE_BUZZER != 1
  (void)rtttl;
  return false;
#else
  if (rtttl == nullptr || rtttl[0] == '\0') {
    portENTER_CRITICAL(&mux);
    bullseyeNoteCount = 0;
    portEXIT_CRITICAL(&mux);
    return true;
  }
  // Validate before publishing a zero count. Parse directly into the inactive
  // custom-note storage so this setup-time path does not add a 1 KiB automatic
  // buffer to the Arduino loop task.
  if (!validBullseyeRtttl(rtttl)) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  bullseyeNoteCount = 0;
  portEXIT_CRITICAL(&mux);
  uint8_t count = 0;
  if (!parseRtttlBounded(rtttl, bullseyeNotes, BULLSEYE_RTTTL_MAX_NOTES, count) ||
      count == 0) {
    return false;
  }
  portENTER_CRITICAL(&mux);
  bullseyeNoteCount = count;
  portEXIT_CRITICAL(&mux);
  return true;
#endif
}

inline bool LocalBuzzer::requestBullseye() {
  BuzzerToneCommand cmd;
  cmd.cue = BuzzerCue::BULLSEYE;
  cmd.valid = true;
  return requestTone(cmd);
}

inline void LocalBuzzer::stopAll() {
  if (!BUZZER_SUPPORT_ENABLED || !ready) {
    return;
  }
  portENTER_CRITICAL(&mux);
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingPulseRate = 0;
  pendingDurationMs = 0;
  stopTone();
  clearPlayback();
  cancelPhaseTimer();
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::stopIf(BuzzerPattern pattern) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || pattern == BuzzerPattern::NONE) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (pending == pattern && pendingCue == BuzzerCue::NONE) {
    pending = BuzzerPattern::NONE;
    pendingCue = BuzzerCue::NONE;
    pendingPulseRate = 0;
    pendingDurationMs = 0;
  }
  if (active != pattern || activeCue != BuzzerCue::NONE) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  const uint32_t nowMs = millis();
  stopTone();
  clearPlayback();
  if (pending == BuzzerPattern::NONE && pendingCue == BuzzerCue::NONE) {
    cancelPhaseTimer();
    portEXIT_CRITICAL(&mux);
    return;
  }
  const BuzzerPattern next = pending;
  const BuzzerCue nextCue = pendingCue;
  const uint8_t nextPulseRate = pendingPulseRate;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingPulseRate = 0;
  pendingDurationMs = 0;
  if (!startPendingLocked(next, nextCue, nextPulseRate, nextDurationMs,
                          nowMs)) {
    cancelPhaseTimer();
  }
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::stopIfCue(BuzzerCue cue) {
  if (!BUZZER_SUPPORT_ENABLED || !ready || cue == BuzzerCue::NONE) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (pendingCue == cue) {
    pending = BuzzerPattern::NONE;
    pendingCue = BuzzerCue::NONE;
    pendingPulseRate = 0;
    pendingDurationMs = 0;
  }
  if (activeCue != cue) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  const uint32_t nowMs = millis();
  stopTone();
  clearPlayback();
  if (pending == BuzzerPattern::NONE && pendingCue == BuzzerCue::NONE) {
    cancelPhaseTimer();
    portEXIT_CRITICAL(&mux);
    return;
  }
  const BuzzerPattern next = pending;
  const BuzzerCue nextCue = pendingCue;
  const uint8_t nextPulseRate = pendingPulseRate;
  const uint32_t nextDurationMs = pendingDurationMs;
  pending = BuzzerPattern::NONE;
  pendingCue = BuzzerCue::NONE;
  pendingPulseRate = 0;
  pendingDurationMs = 0;
  if (!startPendingLocked(next, nextCue, nextPulseRate, nextDurationMs,
                          nowMs)) {
    cancelPhaseTimer();
  }
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::stopExtendedPulse() {
  if (!BUZZER_SUPPORT_ENABLED || !ready) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (buzzerCueIsLooping(pendingCue) || buzzerPatternIsPulseTrain(pending)) {
    pending = BuzzerPattern::NONE;
    pendingCue = BuzzerCue::NONE;
    pendingPulseRate = 0;
    pendingDurationMs = 0;
  }
  const bool infinitePulse =
      looping && deadlineAtMs == 0 &&
      (buzzerCueIsLooping(activeCue) || buzzerPatternIsPulseTrain(active) ||
       rtttlPlayback);
  if (!infinitePulse) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  finish(millis());
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::stopPulseTrains() {
  if (!BUZZER_SUPPORT_ENABLED || !ready) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (buzzerCueIsLooping(pendingCue) || buzzerPatternIsPulseTrain(pending)) {
    pending = BuzzerPattern::NONE;
    pendingCue = BuzzerCue::NONE;
    pendingPulseRate = 0;
    pendingDurationMs = 0;
  }
  if (!(buzzerCueIsLooping(activeCue) || buzzerPatternIsPulseTrain(active) ||
        looping)) {
    portEXIT_CRITICAL(&mux);
    return;
  }
  finish(millis());
  portEXIT_CRITICAL(&mux);
}

inline void LocalBuzzer::service(uint32_t nowMs) {
  if (!BUZZER_SUPPORT_ENABLED || !ready) {
    return;
  }
  portENTER_CRITICAL(&mux);
  if (active == BuzzerPattern::NONE && activeCue == BuzzerCue::NONE) {
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
