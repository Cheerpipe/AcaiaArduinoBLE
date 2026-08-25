#pragma once

#include <stdint.h>

// Melody catalog only. Rate → string mapping is in ShotStopperAlertTone.h.

namespace shotstopper {

enum class BuzzerCue : uint8_t {
  NONE = 0,
  TARE,
  START_TIMER,
  STOP_TIMER,
  TARE_START,
  FIRST_DROP,
  PADDLE_REMINDER,
  SHOT_END,
  SCALE_CONNECTED,
  NO_CUP,
  ABNORMAL_FAST,
  ABNORMAL,
  SCALE_LOST,
  GUARD_STOP,
  NO_SCALE,
  RECOVERY_START,
  NETWORK_RESET_OK,
  FACTORY_RESET_OK,
  RECOVERY_ERROR
};

// Tare
constexpr const char *RTTTL_TARE = "tare:d=16,o=5,b=220:g";
// Start timer
constexpr const char *RTTTL_START_TIMER = "start:d=16,o=5,b=220:c6";
// Stop timer
constexpr const char *RTTTL_STOP_TIMER = "stop:d=16,o=5,b=220:g";
// Tare + start combinado
constexpr const char *RTTTL_TARE_START = "tare_start:d=16,o=5,b=220:g,c6";
// Primeras gotas
constexpr const char *RTTTL_FIRST_DROP = "first_drop:d=32,o=6,b=240:c,e";
// Recordatorio paddle-off
constexpr const char *RTTTL_PADDLE_OFF = "paddle_off:d=16,o=5,b=180:g,16p,g";
// Extra de fin de shot
constexpr const char *RTTTL_SHOT_END = "shot_end:d=8,o=5,b=220:g,16c6";
// Balanza conectada
constexpr const char *RTTTL_SCALE_CONNECTED =
    "scale_connected:d=8,o=5,b=150:c,e,g,c6";
// Arranque bloqueado por taza ausente
constexpr const char *RTTTL_NO_CUP = "no_cup:d=8,o=4,b=180:g,16p,g";
// Extended-shot pulses: B4 (same pitch as the former ABNORMAL cues) with
// on/gap close to the debug 2/3/4/5 Hz trains (20 ms on, b=375 → 32nd=20 ms).
constexpr const char *RTTTL_EXTENDED_PULSE_SLOW =
    "abnormal_slow:d=32,o=4,b=375:b,2p,4p";
constexpr const char *RTTTL_EXTENDED_PULSE_MEDIUM =
    "abnormal_medium:d=32,o=4,b=375:b,2p";
constexpr const char *RTTTL_EXTENDED_PULSE_FAST =
    "abnormal_fast:d=32,o=4,b=375:b,3p,32p";
constexpr const char *RTTTL_EXTENDED_PULSE_RAPID =
    "abnormal_rapid:d=32,o=4,b=375:b,4p,32p";
// Balanza perdida / desconectada
constexpr const char *RTTTL_SCALE_LOST = "scale_lost:d=8,o=5,b=150:c6,g,e,c";
// Fin por guarda A→M
constexpr const char *RTTTL_GUARD_STOP =
    "guard_stop:d=32,o=6,b=240:g,e,16c,32p,16f#";
// Manual sin balanza (BBW on)
constexpr const char *RTTTL_NO_SCALE = "no_scale:d=8,o=4,b=180:g,32p,g,32p,g";
// Recovery: gesto de arranque
constexpr const char *RTTTL_RECOVERY_START =
    "recovery_start:d=16,o=5,b=200:c,g,c6";
// Recovery: reset de red OK
constexpr const char *RTTTL_NETWORK_RESET_OK =
    "network_reset_ok:d=16,o=5,b=220:c,e,g,c6";
// Recovery: factory reset OK
constexpr const char *RTTTL_FACTORY_RESET_OK =
    "factory_reset_ok:d=16,o=5,b=180:c,e,g,8c6";
// Recovery: error
constexpr const char *RTTTL_RECOVERY_ERROR =
    "recovery_error:d=8,o=4,b=180:g,16p,d#,16p,g";

inline const char *rtttlForCue(BuzzerCue cue) {
  switch (cue) {
    case BuzzerCue::TARE:
      return RTTTL_TARE;
    case BuzzerCue::START_TIMER:
      return RTTTL_START_TIMER;
    case BuzzerCue::STOP_TIMER:
      return RTTTL_STOP_TIMER;
    case BuzzerCue::TARE_START:
      return RTTTL_TARE_START;
    case BuzzerCue::FIRST_DROP:
      return RTTTL_FIRST_DROP;
    case BuzzerCue::PADDLE_REMINDER:
      return RTTTL_PADDLE_OFF;
    case BuzzerCue::SHOT_END:
      return RTTTL_SHOT_END;
    case BuzzerCue::SCALE_CONNECTED:
      return RTTTL_SCALE_CONNECTED;
    case BuzzerCue::NO_CUP:
      return RTTTL_NO_CUP;
    case BuzzerCue::ABNORMAL_FAST:
    case BuzzerCue::ABNORMAL:
      break;
    case BuzzerCue::SCALE_LOST:
      return RTTTL_SCALE_LOST;
    case BuzzerCue::GUARD_STOP:
      return RTTTL_GUARD_STOP;
    case BuzzerCue::NO_SCALE:
      return RTTTL_NO_SCALE;
    case BuzzerCue::RECOVERY_START:
      return RTTTL_RECOVERY_START;
    case BuzzerCue::NETWORK_RESET_OK:
      return RTTTL_NETWORK_RESET_OK;
    case BuzzerCue::FACTORY_RESET_OK:
      return RTTTL_FACTORY_RESET_OK;
    case BuzzerCue::RECOVERY_ERROR:
      return RTTTL_RECOVERY_ERROR;
    case BuzzerCue::NONE:
      break;
  }
  return nullptr;
}

inline bool buzzerCueIsLooping(BuzzerCue cue) {
  return cue == BuzzerCue::ABNORMAL_FAST || cue == BuzzerCue::ABNORMAL;
}

inline bool buzzerCueIsHighPriority(BuzzerCue cue) {
  return cue == BuzzerCue::NO_SCALE || cue == BuzzerCue::GUARD_STOP ||
         cue == BuzzerCue::SCALE_CONNECTED || cue == BuzzerCue::SCALE_LOST;
}

}  // namespace shotstopper
