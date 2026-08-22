#pragma once

#include <stdint.h>

#include "ShotStopperAlert.h"
#include "ShotStopperBuzzerRtttl.h"
#include "ShotStopperDomain.h"

namespace shotstopper {

struct BuzzerToneCommand {
  BuzzerCue cue = BuzzerCue::NONE;
  BuzzerPattern pattern = BuzzerPattern::NONE;
  bool looping = false;
  uint32_t durationMs = 0;
  bool valid = false;
};

inline BuzzerPattern buzzerPatternForCue(BuzzerCue cue) {
  switch (cue) {
    case BuzzerCue::TARE:
    case BuzzerCue::START_TIMER:
    case BuzzerCue::STOP_TIMER:
    case BuzzerCue::TARE_START:
    case BuzzerCue::FIRST_DROP:
    case BuzzerCue::PADDLE_REMINDER:
      return BuzzerPattern::SINGLE;
    case BuzzerCue::SHOT_END:
      return BuzzerPattern::LONG;
    case BuzzerCue::SCALE_CONNECTED:
      return BuzzerPattern::ECHO;
    case BuzzerCue::NO_CUP:
      return BuzzerPattern::DOUBLE;
    case BuzzerCue::ABNORMAL_FAST:
    case BuzzerCue::ABNORMAL:
      return BuzzerPattern::PULSE_TRAIN;
    case BuzzerCue::SCALE_LOST:
      return BuzzerPattern::ECHO_INVERTED;
    case BuzzerCue::GUARD_STOP:
    case BuzzerCue::NO_SCALE:
      return BuzzerPattern::TRIPLE;
    case BuzzerCue::RECOVERY_START:
      return BuzzerPattern::RECOVERY_LONG;
    case BuzzerCue::NETWORK_RESET_OK:
      return BuzzerPattern::RECOVERY_NETWORK_OK;
    case BuzzerCue::FACTORY_RESET_OK:
      return BuzzerPattern::RECOVERY_FACTORY_OK;
    case BuzzerCue::RECOVERY_ERROR:
      return BuzzerPattern::RECOVERY_ERROR;
    case BuzzerCue::NONE:
      break;
  }
  return BuzzerPattern::NONE;
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

// Stage 4: event → active pattern or passive RTTTL cue. Disabled extended
// pulse yields an invalid command (silent).
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
    cmd.pattern = buzzerPatternForExtendedPulseRate(rate);
    if (cmd.pattern == BuzzerPattern::NONE) {
      cmd.cue = BuzzerCue::NONE;
      return cmd;
    }
    cmd.looping = true;
    cmd.valid = true;
    return cmd;
  }
  cmd.pattern = buzzerPatternForCue(cmd.cue);
  cmd.looping = buzzerCueIsLooping(cmd.cue);
  cmd.valid = cmd.pattern != BuzzerPattern::NONE;
  return cmd;
}

inline BuzzerToneCommand deriveRecoveryTone(BuzzerCue cue) {
  BuzzerToneCommand cmd;
  cmd.cue = cue;
  cmd.pattern = buzzerPatternForCue(cue);
  cmd.valid = cmd.pattern != BuzzerPattern::NONE;
  return cmd;
}

}  // namespace shotstopper
