#pragma once

#include <stdint.h>

#include "ShotStopperAlert.h"
#include "ShotStopperBuzzerRtttl.h"
#include "ShotStopperDomain.h"

namespace shotstopper {

struct BuzzerToneCommand {
  BuzzerCue cue = BuzzerCue::NONE;
  uint8_t pulseRate = 0;
  bool looping = false;
  uint32_t durationMs = 0;
  bool valid = false;
};

inline const char *rtttlForExtendedPulseRate(uint8_t rate) {
  switch (static_cast<ExtendedPulseRate>(rate)) {
    case ExtendedPulseRate::SLOW:
      return RTTTL_EXTENDED_PULSE_SLOW;
    case ExtendedPulseRate::MEDIUM:
      return RTTTL_EXTENDED_PULSE_MEDIUM;
    case ExtendedPulseRate::FAST:
      return RTTTL_EXTENDED_PULSE_FAST;
    case ExtendedPulseRate::RAPID:
      return RTTTL_EXTENDED_PULSE_RAPID;
    case ExtendedPulseRate::OFF:
      break;
  }
  return nullptr;
}

inline BuzzerCue buzzerCueForAlertEvent(AlertEvent event, bool slowExtended) {
  switch (event) {
    case AlertEvent::TARE:
      return BuzzerCue::TARE;
    case AlertEvent::START_TIMER:
      return BuzzerCue::START_TIMER;
    case AlertEvent::STOP_TIMER:
      return BuzzerCue::STOP_TIMER;
    case AlertEvent::TARE_START:
      return BuzzerCue::TARE_START;
    case AlertEvent::FIRST_DROP:
      return BuzzerCue::FIRST_DROP;
    case AlertEvent::PADDLE_REMINDER:
      return BuzzerCue::PADDLE_REMINDER;
    case AlertEvent::COMPLETION_EXTRA:
      return BuzzerCue::SHOT_END;
    case AlertEvent::SCALE_CONNECTED:
      return BuzzerCue::SCALE_CONNECTED;
    case AlertEvent::CUP_START_BLOCKED:
      return BuzzerCue::NO_CUP;
    case AlertEvent::EXTENDED_PULSE:
      return slowExtended ? BuzzerCue::ABNORMAL : BuzzerCue::ABNORMAL_FAST;
    case AlertEvent::SCALE_LOST:
      return BuzzerCue::SCALE_LOST;
    case AlertEvent::ATM_END:
      return BuzzerCue::GUARD_STOP;
    case AlertEvent::MANUAL_NO_SCALE:
      return BuzzerCue::NO_SCALE;
  }
  return BuzzerCue::NONE;
}

// Stage 4: event → RTTTL cue. Disabled extended pulse yields an invalid
// command (silent).
inline BuzzerToneCommand deriveBuzzerTone(AlertEvent event, bool slowExtended,
                                         uint8_t extendedPulseRate,
                                         uint8_t slowExtendedPulseRate) {
  BuzzerToneCommand cmd;
  cmd.cue = buzzerCueForAlertEvent(event, slowExtended);
  if (cmd.cue == BuzzerCue::NONE) {
    return cmd;
  }
  if (event == AlertEvent::EXTENDED_PULSE) {
    const uint8_t rate =
        slowExtended ? slowExtendedPulseRate : extendedPulseRate;
    if (rtttlForExtendedPulseRate(rate) == nullptr) {
      cmd.cue = BuzzerCue::NONE;
      return cmd;
    }
    cmd.pulseRate = rate;
    cmd.looping = true;
    cmd.valid = true;
    return cmd;
  }
  cmd.looping = buzzerCueIsLooping(cmd.cue);
  cmd.valid = rtttlForCue(cmd.cue) != nullptr;
  return cmd;
}

inline BuzzerToneCommand deriveRecoveryTone(BuzzerCue cue) {
  BuzzerToneCommand cmd;
  cmd.cue = cue;
  cmd.looping = buzzerCueIsLooping(cue);
  cmd.valid = rtttlForCue(cue) != nullptr;
  return cmd;
}

}  // namespace shotstopper
