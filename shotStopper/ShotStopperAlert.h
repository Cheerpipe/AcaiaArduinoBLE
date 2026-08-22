#pragma once

#include <stdint.h>

#include "ShotStopperDomain.h"

namespace shotstopper {

enum class AlertEvent : uint8_t {
  TARE = 0,
  START_TIMER,
  STOP_TIMER,
  TARE_START,
  FIRST_DROP,
  PADDLE_REMINDER,
  COMPLETION_EXTRA,
  SCALE_LOST,
  ATM_END,
  MANUAL_NO_SCALE,
  CUP_START_BLOCKED,
  EXTENDED_PULSE,
  SCALE_CONNECTED
};

enum class AlertKind : uint8_t {
  Independent = 0,
  CommandImmediate,
  CommandFallback,
  Recovery
};

enum class AlertSink : uint8_t { None = 0, Scale, Buzzer };

struct AlertChannelContext {
  AlertOutputChannel channel = AlertOutputChannel::SCALE_ONLY;
  bool soundAlertsEnabled = true;
  bool buzzerSupportEnabled = false;
  bool buzzerReady = false;
  bool scaleAvailable = false;
  bool scaleSupportsIndependentBeep = false;
  bool scaleSupportsCommandFeedback = false;
  bool commandFeedbackExpected = false;
  bool commandAttempted = false;
  bool writeSucceeded = false;
};

inline bool alertEventScaleCapable(AlertEvent event) {
  switch (event) {
    case AlertEvent::SCALE_LOST:
    case AlertEvent::ATM_END:
    case AlertEvent::MANUAL_NO_SCALE:
    case AlertEvent::CUP_START_BLOCKED:
    case AlertEvent::EXTENDED_PULSE:
    case AlertEvent::SCALE_CONNECTED:
      return false;
    default:
      return true;
  }
}

inline bool alertEventQueuesScaleBeep(AlertEvent event) {
  return event == AlertEvent::FIRST_DROP ||
         event == AlertEvent::PADDLE_REMINDER ||
         event == AlertEvent::COMPLETION_EXTRA;
}

}  // namespace shotstopper
