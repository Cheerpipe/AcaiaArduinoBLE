#pragma once

#include "ShotStopperAlert.h"

namespace shotstopper {

// Stages 1–3: mute gate, configured channel, then Scale / Buzzer / None.
// Never both sinks. Matches emitAlert / commandAlertUsesBuzzer / emitCommandAlert.
inline AlertSink selectAlertSink(AlertKind kind, AlertEvent event,
                                 const AlertChannelContext &ctx) {
  if (kind == AlertKind::Recovery) {
    return ctx.buzzerSupportEnabled ? AlertSink::Buzzer : AlertSink::None;
  }
  if (!ctx.soundAlertsEnabled) {
    return AlertSink::None;
  }

  const AlertOutputChannel channel = ctx.channel;

  if (kind == AlertKind::CommandImmediate) {
    if (!ctx.buzzerReady) {
      return AlertSink::None;
    }
    if (channel == AlertOutputChannel::BUZZER_ONLY) {
      return AlertSink::Buzzer;
    }
    if (channel == AlertOutputChannel::SCALE_PRIORITY) {
      return (!ctx.scaleAvailable || !ctx.scaleSupportsCommandFeedback)
                 ? AlertSink::Buzzer
                 : AlertSink::None;
    }
    return AlertSink::None;
  }

  if (kind == AlertKind::CommandFallback) {
    if (channel == AlertOutputChannel::BUZZER_ONLY ||
        channel == AlertOutputChannel::SCALE_ONLY) {
      return AlertSink::None;
    }
    if (!ctx.commandFeedbackExpected) {
      return AlertSink::None;
    }
    if (!ctx.commandAttempted || !ctx.writeSucceeded) {
      return ctx.buzzerReady ? AlertSink::Buzzer : AlertSink::None;
    }
    return AlertSink::None;
  }

  if (!alertEventScaleCapable(event)) {
    if (channel == AlertOutputChannel::SCALE_ONLY) {
      return AlertSink::None;
    }
    return AlertSink::Buzzer;
  }
  if (channel == AlertOutputChannel::BUZZER_ONLY) {
    return AlertSink::Buzzer;
  }
  const bool scaleBeep =
      alertEventQueuesScaleBeep(event) && ctx.scaleAvailable &&
      ctx.scaleSupportsIndependentBeep;
  if (channel == AlertOutputChannel::SCALE_ONLY) {
    return scaleBeep ? AlertSink::Scale : AlertSink::None;
  }
  if (scaleBeep) {
    return AlertSink::Scale;
  }
  return AlertSink::Buzzer;
}

}  // namespace shotstopper
