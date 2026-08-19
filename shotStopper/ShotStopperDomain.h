#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ShotStopperSafety.h"
#include "ShotStopperHwmon.h"

namespace shotstopper {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t CONFIG_SCHEMA_VERSION = 10;
constexpr size_t PREFERRED_SCALE_MAC_CAPACITY = 18;
constexpr size_t PREFERRED_SCALE_NAME_CAPACITY = 32;
constexpr size_t SCALE_HISTORY_CAPACITY = 8;
constexpr uint32_t SCALE_PAIRING_DISCOVERY_PAUSE_MS = 30000;
constexpr size_t NTP_SERVER_HOST_CAPACITY = 64;
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 3600UL * 1000UL;
constexpr uint32_t NTP_UNSYNCED_RETRY_MS = 15UL * 1000UL;
constexpr uint32_t NTP_FIRST_SYNC_TIMEOUT_MS = 10UL * 1000UL;
constexpr uint32_t NTP_STA_SETTLE_MS = 8000;
constexpr uint32_t NTP_STALE_AFTER_MS = 24UL * 3600UL * 1000UL;
constexpr uint8_t NTP_MAX_CONSECUTIVE_FAILURES = 255;

enum class NtpServerPreset : uint8_t {
  POOL = 0,
  GOOGLE = 1,
  CLOUDFLARE = 2,
  NIST = 3
};

// Paired-scale lock.
enum class ScaleMacCacheMode : uint8_t {
  // Named OFF (not DISABLED): ESP32 Arduino defines a DISABLED GPIO macro.
  OFF = 0,   // Name scan; connect first compatible; do not lock preferred.
  FULL = 1,  // Name scan; connect only preferred (or first if none yet).
};

// BLE-seen scales remembered for the preferred-scale dropdown (NVS + status).
struct ScaleHistoryEntry {
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  uint32_t lastSeenSeq = 0;
};

inline bool validScaleMacCacheMode(uint8_t mode) {
  return mode == static_cast<uint8_t>(ScaleMacCacheMode::OFF) ||
         mode == static_cast<uint8_t>(ScaleMacCacheMode::FULL);
}

inline const char *scaleMacCacheModeId(uint8_t mode) {
  switch (static_cast<ScaleMacCacheMode>(mode)) {
    case ScaleMacCacheMode::OFF:
      return "disabled";
    case ScaleMacCacheMode::FULL:
    default:
      return "full";
  }
}

inline bool parseScaleMacCacheMode(const char *text, uint8_t &mode) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "disabled") == 0) {
    mode = static_cast<uint8_t>(ScaleMacCacheMode::OFF);
    return true;
  }
  if (strcmp(text, "full") == 0) {
    mode = static_cast<uint8_t>(ScaleMacCacheMode::FULL);
    return true;
  }
  return false;
}

// Natural = current FW paddle semantics. Original = legacy Shot Stopper
// start gesture (BBW+scale only). Auto = BBW finish regardless of paddle
// ON/OFF (auto-natural vs auto-original in the same shot).
enum class PaddleMode : uint8_t {
  NATURAL = 0,
  ORIGINAL = 1,
  AUTO = 2
};

inline bool validPaddleMode(uint8_t mode) {
  return mode == static_cast<uint8_t>(PaddleMode::NATURAL) ||
         mode == static_cast<uint8_t>(PaddleMode::ORIGINAL) ||
         mode == static_cast<uint8_t>(PaddleMode::AUTO);
}

inline const char *paddleModeId(uint8_t mode) {
  switch (static_cast<PaddleMode>(mode)) {
    case PaddleMode::ORIGINAL:
      return "original";
    case PaddleMode::AUTO:
      return "auto";
    case PaddleMode::NATURAL:
    default:
      return "natural";
  }
}

inline bool parsePaddleMode(const char *text, uint8_t &mode) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "natural") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::NATURAL);
    return true;
  }
  if (strcmp(text, "original") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::ORIGINAL);
    return true;
  }
  if (strcmp(text, "auto") == 0) {
    mode = static_cast<uint8_t>(PaddleMode::AUTO);
    return true;
  }
  return false;
}

inline bool validNtpHostnameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.';
}

inline bool validNtpHostname(const char *host) {
  if (host == nullptr) {
    return false;
  }
  const size_t length = strnlen(host, NTP_SERVER_HOST_CAPACITY);
  if (length == 0 || length >= NTP_SERVER_HOST_CAPACITY) {
    return false;
  }
  if (host[0] == '-' || host[0] == '.' || host[length - 1] == '-' ||
      host[length - 1] == '.') {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    if (!validNtpHostnameChar(host[index])) {
      return false;
    }
  }
  return true;
}

inline const char *ntpPresetHostname(uint8_t preset) {
  switch (preset) {
    case static_cast<uint8_t>(NtpServerPreset::GOOGLE):
      return "time.google.com";
    case static_cast<uint8_t>(NtpServerPreset::CLOUDFLARE):
      return "time.cloudflare.com";
    case static_cast<uint8_t>(NtpServerPreset::NIST):
      return "time.nist.gov";
    case static_cast<uint8_t>(NtpServerPreset::POOL):
    default:
      return "pool.ntp.org";
  }
}
constexpr int16_t MIN_TIMEZONE_OFFSET_MINUTES = -720;
constexpr int16_t MAX_TIMEZONE_OFFSET_MINUTES = 840;
constexpr int16_t DEFAULT_TIMEZONE_OFFSET_MINUTES = 0;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS = 10000;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_INTERVAL_MS = 5000;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_INTERVAL_MS = 60000;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    15UL * 60UL * 1000UL;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_MAX_DURATION_MS = 60000UL;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    60UL * 60UL * 1000UL;
constexpr uint32_t DEFAULT_LAST_SHOT_COOLDOWN_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t MIN_LAST_SHOT_COOLDOWN_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t MAX_LAST_SHOT_COOLDOWN_MS = 240UL * 60UL * 1000UL;
constexpr uint32_t HARD_MAX_CN9_CLOSED_MS = 60000;
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS = 50000;
constexpr uint32_t DEFAULT_RINSE_GESTURE_MS = 1000;
constexpr uint32_t DEFAULT_RINSE_DURATION_MS = 4000;
constexpr uint32_t DEFAULT_RETARE_WINDOW_MS = 4000;
constexpr uint32_t DEFAULT_BBW_PROTECTION_MS = 12000;
constexpr uint32_t MIN_BBW_PROTECTION_AFTER_RETARE_MS = 3000;
constexpr float DEFAULT_MINIMUM_CUP_WEIGHT_G = 10.0f;
constexpr float MIN_MINIMUM_CUP_WEIGHT_G = 1.0f;
constexpr float MAX_MINIMUM_CUP_WEIGHT_G = 500.0f;
constexpr uint32_t MIN_RETARE_WINDOW_MS = 500;
constexpr uint32_t MAX_RETARE_WINDOW_MS = 10000;
constexpr uint32_t MIN_BBW_PROTECTION_MS = 500;
constexpr uint32_t MAX_BBW_PROTECTION_MS = 30000;
constexpr float DEFAULT_RETARE_STABILITY_TOLERANCE_G = 2.0f;
constexpr uint8_t DEFAULT_RETARE_STABILITY_SAMPLES = 3;
constexpr uint32_t DEFAULT_RETARE_STABILITY_MAX_GAP_MS = 500;
constexpr uint32_t DEFAULT_RETARE_STABILITY_MIN_DURATION_MS = 300;
constexpr uint8_t MIN_RETARE_STABILITY_SAMPLES = 2;
constexpr uint8_t MAX_RETARE_STABILITY_SAMPLES = 10;
constexpr float MIN_RETARE_STABILITY_TOLERANCE_G = 0.1f;
constexpr float MAX_RETARE_STABILITY_TOLERANCE_G = 20.0f;
constexpr uint32_t MIN_RETARE_STABILITY_MAX_GAP_MS = 100;
constexpr uint32_t MAX_RETARE_STABILITY_MAX_GAP_MS = 5000;
constexpr uint32_t MIN_RETARE_STABILITY_MIN_DURATION_MS = 0;
constexpr uint32_t MAX_RETARE_STABILITY_MIN_DURATION_MS = 2000;
constexpr uint8_t FIRST_DROP_CONFIRMATION_SAMPLES = 2;
constexpr uint8_t MIN_GOAL_WEIGHT_G = 10;
constexpr uint8_t MAX_GOAL_WEIGHT_G = 200;
constexpr uint8_t DEFAULT_GOAL_WEIGHT_G = 36;
constexpr float DEFAULT_MAX_RECOVERY_WEIGHT_G = 42.5f;
constexpr float MIN_MAX_RECOVERY_WEIGHT_G = 10.0f;
constexpr float MAX_MAX_RECOVERY_WEIGHT_G = 200.0f;
constexpr uint32_t DEFAULT_MIN_BREW_TIME_MS = 28000;
constexpr uint32_t MIN_MIN_BREW_TIME_MS = 5000;
constexpr uint32_t MAX_MIN_BREW_TIME_MS = 55000;
constexpr float DEFAULT_MIN_RECOVERY_WEIGHT_G = 34.0f;
constexpr float MIN_MIN_RECOVERY_WEIGHT_G = 10.0f;
constexpr float MAX_MIN_RECOVERY_WEIGHT_G = 200.0f;
constexpr uint32_t DEFAULT_MAX_BREW_TIME_MS = 44000;
constexpr uint32_t MIN_MAX_BREW_TIME_MS = 5000;
constexpr uint32_t MAX_MAX_BREW_TIME_MS = 55000;
// Extra delay after measured scale start lag before stopping the scale timer at
// shot end (lets the scale display catch up to CN9 time).
constexpr uint32_t DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS = 100;
constexpr uint32_t MIN_SCALE_TIMER_STOP_EXTRA_DELAY_MS = 0;
constexpr uint32_t MAX_SCALE_TIMER_STOP_EXTRA_DELAY_MS = 1000;
constexpr uint32_t DEFAULT_DRIP_DELAY_MS = 3000;
constexpr uint32_t MIN_DRIP_DELAY_MS = 0;
constexpr uint32_t MAX_DRIP_DELAY_MS = 10000;
constexpr uint32_t MAX_SCALE_TIMER_STOP_CATCHUP_MS = 2000;
constexpr float MAX_OFFSET_G = 5.0f;
constexpr float DEFAULT_WEIGHT_OFFSET_G = 1.5f;
constexpr size_t AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT = 5;
constexpr uint32_t DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS = 32000;
constexpr uint16_t AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS = 320;  // 32.0 s
constexpr uint32_t DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS =
    DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
constexpr uint32_t MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS = 10000;
constexpr float AUTO_TO_MANUAL_GUARD_SAMPLE_MAX_ERROR_RATIO = 0.10f;
constexpr size_t WIFI_SSID_CAPACITY = 33;
constexpr size_t WIFI_PASSWORD_CAPACITY = 64;
constexpr size_t WEB_COMMAND_QUEUE_LENGTH = 8;
constexpr size_t DEBUG_EVENT_CAPACITY = 192;
constexpr uint32_t MAX_AUTOMATION_WEIGHT_AGE_MS = 1000;
constexpr float MIN_AUTOMATION_WEIGHT_G = -500.0f;
constexpr float MAX_AUTOMATION_WEIGHT_G = 1000.0f;
constexpr float MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S = 100.0f;
constexpr float AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G = 20.0f;
constexpr float POST_TARE_BASELINE_MAX_ABS_G = 50.0f;
constexpr uint32_t POST_TARE_BASELINE_GRACE_MS = 2000;
// Ignore tare noise around 0; a lifted cup drops several grams below this.
constexpr float DEFAULT_CUP_REMOVED_WEIGHT_G = -3.0f;
constexpr float MIN_CUP_REMOVED_WEIGHT_G = -50.0f;
constexpr float MAX_CUP_REMOVED_WEIGHT_G = -0.1f;
constexpr float DEFAULT_CUP_PRESENT_WEIGHT_G = 3.0f;
constexpr float MIN_CUP_PRESENT_WEIGHT_G = 0.1f;
constexpr float MAX_CUP_PRESENT_WEIGHT_G = 50.0f;
constexpr float MAX_PARSED_WEIGHT_G = 10000.0f;
constexpr uint8_t DIRECT_STOP_CONFIRMATION_SAMPLES = 2;
constexpr uint8_t WEIGHT_RECOVERY_CONFIRMATION_SAMPLES = 3;
constexpr uint32_t DIRECT_STOP_CONFIRMATION_WINDOW_MS = 1000;
constexpr float MAX_RECOVERY_WEIGHT_DROP_G = 2.0f;
constexpr size_t WEIGHT_TREND_POINT_COUNT = 10;
constexpr float WEIGHT_TREND_MIN_LAST_SAMPLE_G = 10.0f;
constexpr float WEIGHT_TREND_MIN_HORIZON_S = 0.25f;

#ifndef SHOT_STOPPER_ENABLE_REMOTE_CN9
#define SHOT_STOPPER_ENABLE_REMOTE_CN9 0
#endif

constexpr bool REMOTE_CN9_CONTROL_ENABLED =
    SHOT_STOPPER_ENABLE_REMOTE_CN9 == 1;
static_assert(SHOT_STOPPER_ENABLE_REMOTE_CN9 == 0 ||
                  SHOT_STOPPER_ENABLE_REMOTE_CN9 == 1,
              "SHOT_STOPPER_ENABLE_REMOTE_CN9 must be 0 or 1");

#ifndef SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER 0
#endif

constexpr bool BUZZER_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_BUZZER != 0;
constexpr bool BUZZER_ACTIVE_DRIVE = SHOT_STOPPER_ENABLE_BUZZER == 2;
static_assert(SHOT_STOPPER_ENABLE_BUZZER == 0 ||
                  SHOT_STOPPER_ENABLE_BUZZER == 1 ||
                  SHOT_STOPPER_ENABLE_BUZZER == 2,
              "SHOT_STOPPER_ENABLE_BUZZER must be 0, 1, or 2");

enum class BuzzerPattern : uint8_t {
  NONE = 0,
  SINGLE = 1,
  TRIPLE = 2,
  DOUBLE = 3,
  LONG = 4,
  PULSE_TRAIN = 5,
  PULSE_3HZ = 6,
  PULSE_4HZ = 7,
  PULSE_5HZ = 8,
  CHIME = 9,
  SWING = 10,
  ECHO = 11,
  MORSE = 12,
  SNAP = 13,
  ECHO_INVERTED = 14,
  RECOVERY_LONG = 15,
  RECOVERY_NETWORK_OK = 16,
  RECOVERY_FACTORY_OK = 17,
  RECOVERY_ERROR = 18
};

inline bool buzzerPatternIsPulseTrain(BuzzerPattern pattern) {
  return pattern == BuzzerPattern::PULSE_TRAIN ||
         pattern == BuzzerPattern::PULSE_3HZ ||
         pattern == BuzzerPattern::PULSE_4HZ ||
         pattern == BuzzerPattern::PULSE_5HZ;
}

inline bool buzzerPatternIsSequence(BuzzerPattern pattern) {
  return pattern == BuzzerPattern::CHIME ||
         pattern == BuzzerPattern::SWING ||
         pattern == BuzzerPattern::ECHO ||
         pattern == BuzzerPattern::ECHO_INVERTED ||
         pattern == BuzzerPattern::MORSE ||
         pattern == BuzzerPattern::SNAP ||
         pattern == BuzzerPattern::RECOVERY_NETWORK_OK ||
         pattern == BuzzerPattern::RECOVERY_FACTORY_OK ||
         pattern == BuzzerPattern::RECOVERY_ERROR;
}

inline bool parseBuzzerPatternId(const char *id, BuzzerPattern &out) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  if (strcmp(id, "short") == 0) {
    out = BuzzerPattern::SINGLE;
    return true;
  }
  if (strcmp(id, "long") == 0) {
    out = BuzzerPattern::LONG;
    return true;
  }
  if (strcmp(id, "double") == 0) {
    out = BuzzerPattern::DOUBLE;
    return true;
  }
  if (strcmp(id, "triple") == 0) {
    out = BuzzerPattern::TRIPLE;
    return true;
  }
  if (strcmp(id, "pulse") == 0 || strcmp(id, "pulse2") == 0) {
    out = BuzzerPattern::PULSE_TRAIN;
    return true;
  }
  if (strcmp(id, "pulse3") == 0) {
    out = BuzzerPattern::PULSE_3HZ;
    return true;
  }
  if (strcmp(id, "pulse4") == 0) {
    out = BuzzerPattern::PULSE_4HZ;
    return true;
  }
  if (strcmp(id, "pulse5") == 0) {
    out = BuzzerPattern::PULSE_5HZ;
    return true;
  }
  if (strcmp(id, "chime") == 0) {
    out = BuzzerPattern::CHIME;
    return true;
  }
  if (strcmp(id, "swing") == 0) {
    out = BuzzerPattern::SWING;
    return true;
  }
  if (strcmp(id, "echo") == 0) {
    out = BuzzerPattern::ECHO;
    return true;
  }
  if (strcmp(id, "echoinv") == 0) {
    out = BuzzerPattern::ECHO_INVERTED;
    return true;
  }
  if (strcmp(id, "morse") == 0) {
    out = BuzzerPattern::MORSE;
    return true;
  }
  if (strcmp(id, "snap") == 0) {
    out = BuzzerPattern::SNAP;
    return true;
  }
  return false;
}

constexpr uint8_t BOOKOO_BEEP_LEVEL_MAX = 5;
constexpr uint8_t DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL = 4;

enum class BookooDebugAction : uint8_t {
  START = 0,
  STOP,
  TARE,
  COMBINED,
  BEEP,
  VOLUME
};

inline bool parseBookooDebugActionId(const char *id, BookooDebugAction &out) {
  if (id == nullptr || id[0] == '\0') {
    return false;
  }
  if (strcmp(id, "start") == 0) {
    out = BookooDebugAction::START;
    return true;
  }
  if (strcmp(id, "stop") == 0) {
    out = BookooDebugAction::STOP;
    return true;
  }
  if (strcmp(id, "tare") == 0) {
    out = BookooDebugAction::TARE;
    return true;
  }
  if (strcmp(id, "combined") == 0) {
    out = BookooDebugAction::COMBINED;
    return true;
  }
  if (strcmp(id, "beep") == 0) {
    out = BookooDebugAction::BEEP;
    return true;
  }
  if (strcmp(id, "volume") == 0) {
    out = BookooDebugAction::VOLUME;
    return true;
  }
  return false;
}

// Where alert sounds are routed when local buzzer support is compiled in.
enum class AlertOutputChannel : uint8_t {
  SCALE_ONLY = 0,
  BUZZER_ONLY = 1,
  SCALE_PRIORITY = 2
};

inline bool validAlertOutputChannel(uint8_t channel) {
  return channel <= static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
}

constexpr AlertOutputChannel DEFAULT_ALERT_OUTPUT_CHANNEL =
    BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                           : AlertOutputChannel::SCALE_ONLY;

inline const char *alertOutputChannelId(uint8_t channel) {
  switch (static_cast<AlertOutputChannel>(channel)) {
    case AlertOutputChannel::SCALE_ONLY:
      return "scale_only";
    case AlertOutputChannel::BUZZER_ONLY:
      return "buzzer_only";
    case AlertOutputChannel::SCALE_PRIORITY:
      return "scale_priority";
  }
  return BUZZER_SUPPORT_ENABLED ? "buzzer_only" : "scale_only";
}

inline bool parseAlertOutputChannel(const char *text, uint8_t &channel) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "scale_only") == 0) {
    channel = static_cast<uint8_t>(AlertOutputChannel::SCALE_ONLY);
    return true;
  }
  if (strcmp(text, "buzzer_only") == 0) {
    channel = static_cast<uint8_t>(AlertOutputChannel::BUZZER_ONLY);
    return true;
  }
  if (strcmp(text, "scale_priority") == 0) {
    channel = static_cast<uint8_t>(AlertOutputChannel::SCALE_PRIORITY);
    return true;
  }
  return false;
}

// Builds without buzzer always behave as scale-only regardless of stored value.
inline AlertOutputChannel effectiveAlertOutputChannel(uint8_t stored) {
  if (!BUZZER_SUPPORT_ENABLED) {
    return AlertOutputChannel::SCALE_ONLY;
  }
  if (!validAlertOutputChannel(stored)) {
    return DEFAULT_ALERT_OUTPUT_CHANNEL;
  }
  return static_cast<AlertOutputChannel>(stored);
}

// Local-buzzer pulse rate while Fast/Slow extraction guards are extended.
enum class ExtendedPulseRate : uint8_t {
  // Named OFF (not DISABLED): ESP32 Arduino defines a DISABLED GPIO macro.
  OFF = 0,
  SLOW = 1,
  MEDIUM = 2,
  FAST = 3,
  RAPID = 4
};

constexpr ExtendedPulseRate DEFAULT_EXTENDED_PULSE_RATE = ExtendedPulseRate::FAST;

inline bool validExtendedPulseRate(uint8_t rate) {
  return rate <= static_cast<uint8_t>(ExtendedPulseRate::RAPID);
}

inline const char *extendedPulseRateId(uint8_t rate) {
  switch (static_cast<ExtendedPulseRate>(rate)) {
    case ExtendedPulseRate::OFF:
      return "disabled";
    case ExtendedPulseRate::SLOW:
      return "slow";
    case ExtendedPulseRate::MEDIUM:
      return "medium";
    case ExtendedPulseRate::FAST:
      return "fast";
    case ExtendedPulseRate::RAPID:
      return "rapid";
  }
  return "fast";
}

inline bool parseExtendedPulseRate(const char *text, uint8_t &rate) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "disabled") == 0) {
    rate = static_cast<uint8_t>(ExtendedPulseRate::OFF);
    return true;
  }
  if (strcmp(text, "slow") == 0) {
    rate = static_cast<uint8_t>(ExtendedPulseRate::SLOW);
    return true;
  }
  if (strcmp(text, "medium") == 0) {
    rate = static_cast<uint8_t>(ExtendedPulseRate::MEDIUM);
    return true;
  }
  if (strcmp(text, "fast") == 0) {
    rate = static_cast<uint8_t>(ExtendedPulseRate::FAST);
    return true;
  }
  if (strcmp(text, "rapid") == 0) {
    rate = static_cast<uint8_t>(ExtendedPulseRate::RAPID);
    return true;
  }
  return false;
}

inline BuzzerPattern buzzerPatternForExtendedPulseRate(uint8_t rate) {
  switch (static_cast<ExtendedPulseRate>(rate)) {
    case ExtendedPulseRate::SLOW:
      return BuzzerPattern::PULSE_TRAIN;
    case ExtendedPulseRate::MEDIUM:
      return BuzzerPattern::PULSE_3HZ;
    case ExtendedPulseRate::FAST:
      return BuzzerPattern::PULSE_4HZ;
    case ExtendedPulseRate::RAPID:
      return BuzzerPattern::PULSE_5HZ;
    case ExtendedPulseRate::OFF:
      break;
  }
  return BuzzerPattern::NONE;
}

enum class StopperState : uint8_t {
  REQUIRES_OFF,
  READY,
  BREW,
  RINSE,
  MANUAL_NO_SCALE
};

enum class ControlSource : uint8_t {
  NONE,
  PHYSICAL,
  WEB
};

enum class WeightStreamState : uint8_t {
  NO_SAMPLE,
  FRESH,
  STALE,
  ANOMALOUS,
  OVERLOAD
};

enum class WeightControlState : uint8_t {
  INACTIVE,
  VALIDATING,
  ACTIVE,
  SUSPENDED,
  FAULT_STOPPED
};

inline const char *weightStreamStateName(WeightStreamState state) {
  switch (state) {
    case WeightStreamState::NO_SAMPLE: return "NO_SAMPLE";
    case WeightStreamState::FRESH: return "FRESH";
    case WeightStreamState::STALE: return "STALE";
    case WeightStreamState::ANOMALOUS: return "ANOMALOUS";
    case WeightStreamState::OVERLOAD: return "OVERLOAD";
  }
  return "UNKNOWN";
}

inline const char *weightControlStateName(WeightControlState state) {
  switch (state) {
    case WeightControlState::INACTIVE: return "INACTIVE";
    case WeightControlState::VALIDATING: return "VALIDATING";
    case WeightControlState::ACTIVE: return "ACTIVE";
    case WeightControlState::SUSPENDED: return "SUSPENDED";
    case WeightControlState::FAULT_STOPPED: return "FAULT_STOPPED";
  }
  return "UNKNOWN";
}

inline const char *stopperStateName(StopperState state) {
  switch (state) {
    case StopperState::REQUIRES_OFF: return "REQUIRES_OFF";
    case StopperState::READY: return "READY";
    case StopperState::BREW: return "BREW";
    case StopperState::RINSE: return "RINSE";
    case StopperState::MANUAL_NO_SCALE: return "MANUAL_NO_SCALE";
  }
  return "UNKNOWN";
}

enum class EndReason : uint8_t {
  NONE = 0,
  PADDLE = 1,
  // 2 was SCALE_PREDICTION (removed); keep ordinals stable for old logs.
  SCALE_THRESHOLD = 3,
  WEIGHT_ANOMALY = 4,
  GLOBAL_LIMIT = 5,
  CONFIGURED_WALL_LIMIT = 6,
  SHORT_SHOT = 7,
  RINSE_COMPLETE = 8,
  WEB_STOP = 9,
  PHYSICAL_OVERRIDE = 10,
  WEB_HEARTBEAT_TIMEOUT = 11,
  RELAY_SAFETY_FAILURE = 12,
  FAST_EXTRACTION_MAX_WEIGHT = 13,
  FAST_EXTRACTION_MIN_TIME = 14,
  SLOW_EXTRACTION_MAX_TIME = 15,
  SLOW_EXTRACTION_MIN_WEIGHT = 16,
  AUTO_TO_MANUAL_GUARD = 17,
  CUP_REMOVED = 18
};

enum class AutoToManualGuardLimitMode : uint8_t {
  MANUAL = 0,
  AUTO = 1
};

enum class ActualWeightSource : uint8_t {
  NONE = 0,
  POST_DRIP = 1,
  LAST_KNOWN = 2
};

inline uint16_t autoToManualGuardBaselineDs(uint32_t baselineMs) {
  return static_cast<uint16_t>((baselineMs + 50U) / 100U);
}

inline void resetAutoToManualGuardSamples(
    uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT],
    uint32_t baselineMs = DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS) {
  const uint16_t seedDs = autoToManualGuardBaselineDs(baselineMs);
  for (size_t index = 0; index < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++index) {
    samples[index] = seedDs;
  }
}

// Linear least-squares trend over x=0..4, predict x=5 (one step ahead).
inline uint32_t autoToManualGuardTrendMs(
    const uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT],
    uint32_t operationalWallMs) {
  // With n=5 and x=0..4: meanX=2, sum((x-meanX)^2)=10.
  constexpr float meanX = 2.0f;
  constexpr float denom = 10.0f;
  float sumY = 0.0f;
  for (size_t index = 0; index < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++index) {
    sumY += static_cast<float>(samples[index]);
  }
  const float meanY = sumY / static_cast<float>(AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT);
  float numer = 0.0f;
  for (size_t index = 0; index < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++index) {
    const float dx = static_cast<float>(index) - meanX;
    numer += dx * (static_cast<float>(samples[index]) - meanY);
  }
  const float slope = numer / denom;
  const float predictedDs = meanY + slope * (5.0f - meanX);
  if (!isfinite(predictedDs)) {
    return DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
  }
  float predictedMs = predictedDs * 100.0f;
  if (predictedMs < static_cast<float>(MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS)) {
    predictedMs = static_cast<float>(MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS);
  }
  const uint32_t wallCap =
      operationalWallMs < MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS
          ? MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS
          : operationalWallMs;
  if (predictedMs > static_cast<float>(wallCap)) {
    predictedMs = static_cast<float>(wallCap);
  }
  return static_cast<uint32_t>(predictedMs + 0.5f);
}

// Linear least-squares over the last WEIGHT_TREND_POINT_COUNT samples of
// (timeS, weightG); returns the time when weight reaches targetWeightG.
inline float predictedWeightStopTimeS(const float *timeS, const float *weightG,
                                      size_t datapoints, float targetWeightG,
                                      float fallbackEndS) {
  if (timeS == nullptr || weightG == nullptr ||
      datapoints < WEIGHT_TREND_POINT_COUNT || !isfinite(targetWeightG) ||
      !isfinite(fallbackEndS)) {
    return fallbackEndS;
  }
  if (weightG[datapoints - 1] < WEIGHT_TREND_MIN_LAST_SAMPLE_G) {
    return fallbackEndS;
  }

  float sumXY = 0.0f;
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumSquaredX = 0.0f;
  const size_t first = datapoints - WEIGHT_TREND_POINT_COUNT;
  for (size_t i = first; i < datapoints; ++i) {
    if (!isfinite(timeS[i]) || !isfinite(weightG[i])) {
      return fallbackEndS;
    }
    sumXY += timeS[i] * weightG[i];
    sumX += timeS[i];
    sumY += weightG[i];
    sumSquaredX += timeS[i] * timeS[i];
  }

  const float n = static_cast<float>(WEIGHT_TREND_POINT_COUNT);
  const float denominator = n * sumSquaredX - sumX * sumX;
  if (fabsf(denominator) < 0.000001f) {
    return fallbackEndS;
  }

  const float slope = (n * sumXY - sumX * sumY) / denominator;
  if (slope <= 0.0f || !isfinite(slope)) {
    return fallbackEndS;
  }

  const float meanX = sumX / n;
  const float meanY = sumY / n;
  const float intercept = meanY - slope * meanX;
  const float predicted = (targetWeightG - intercept) / slope;
  const float latestSampleS = timeS[datapoints - 1];
  if (!isfinite(predicted) ||
      predicted < latestSampleS + WEIGHT_TREND_MIN_HORIZON_S) {
    return fallbackEndS;
  }
  return predicted;
}

inline uint32_t autoToManualGuardLimitMs(
    bool enabled, AutoToManualGuardLimitMode mode, uint32_t manualLimitMs,
    const uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT],
    uint32_t operationalWallMs) {
  (void)enabled;
  uint32_t limitMs =
      mode == AutoToManualGuardLimitMode::MANUAL
          ? manualLimitMs
          : autoToManualGuardTrendMs(samples, operationalWallMs);
  if (limitMs < MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS) {
    limitMs = MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS;
  }
  if (limitMs > operationalWallMs) {
    limitMs = operationalWallMs;
  }
  return limitMs;
}

inline bool autoToManualGuardSampleErrorOk(float actualWeightG,
                                           uint8_t goalWeightG) {
  if (!isfinite(actualWeightG) || goalWeightG == 0) {
    return false;
  }
  const float goal = static_cast<float>(goalWeightG);
  return fabsf(actualWeightG - goal) <=
         goal * AUTO_TO_MANUAL_GUARD_SAMPLE_MAX_ERROR_RATIO;
}

inline void pushAutoToManualGuardSample(
    uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT], uint16_t durationDs) {
  for (size_t index = 1; index < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++index) {
    samples[index - 1] = samples[index];
  }
  samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT - 1] = durationDs;
}

enum class LogLevel : uint8_t {
  CRITICAL = 0,
  ERROR = 1,
  WARNING = 2,
  INFO = 3,
  DEBUG = 4,
  NONE = 5
};

struct RuntimeConfig {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  // Seed for Reset learned stop offset; factory default remains 1.5 g.
  float weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  // Internal polarity: true disables weight stop. UI/API brewByWeight is the inverse.
  bool timerOnly = false;
  bool canTareStartTimer = true;
  // Added to measured scale start lag before stop at shot end.
  uint32_t scaleTimerStopExtraDelayMs = DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS;
  // Optional beep when the first coffee drop is detected.
  bool firstDropBeep = true;
  // Remind the user to release the physical paddle after CN9 has opened.
  bool paddleReturnReminderBeep = true;
  // Persisted as of schema v4. Schema-v3 records are migrated with alerts on.
  bool soundAlertsMuted = false;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint8_t paddleMode = static_cast<uint8_t>(PaddleMode::NATURAL);
  // Local buzzer alerts (active when SHOT_STOPPER_ENABLE_BUZZER is 1 or 2).
  bool buzzerScaleLostBeep = true;
  bool buzzerAutoToManualGuardEndBeep = true;
  bool buzzerManualNoScaleBeep = true;
  bool buzzerScaleConnectedBeep = true;
  // Onboard GPIO LED HIGH while a BLE scale is connected (machine/alerts).
  bool scaleConnectedLed = true;
  uint8_t buzzerExtendedPulseRate =
      static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE);
  uint8_t buzzerSlowExtendedPulseRate =
      static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE);
  // scale_only | buzzer_only | scale_priority (ignored when buzzer not compiled).
  // Default: buzzer_only with SHOT_STOPPER_ENABLE_BUZZER, else scale_only.
  uint8_t alertOutputChannel =
      static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL);
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
  bool fastExtractionGuardEnabled = true;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBrewTimeMs = DEFAULT_MAX_BREW_TIME_MS;
  bool autoToManualGuardEnabled = true;
  uint8_t autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  uint32_t autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  uint32_t autoToManualGuardBaselineMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
  uint16_t autoToManualGuardSamplesDs[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS};
  uint8_t scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FULL);
  bool bookooMuteOnBuzzerOnly = true;
  uint8_t bookooConnectBeepLevel = DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  float cupPresentWeightG = DEFAULT_CUP_PRESENT_WEIGHT_G;
  float cupRemovedWeightG = DEFAULT_CUP_REMOVED_WEIGHT_G;
  bool avoidBbwShotWithoutScale = true;
  uint32_t lastShotCooldownMs = DEFAULT_LAST_SHOT_COOLDOWN_MS;
  // USB debug spew (paddle/CN9/Wi-Fi traces). CLI replies stay independent.
  bool serialDebugOutput = false;
  // Minimum level retained in the RAM debug ring (WebUI Log). NONE disables.
  uint8_t ringRetainLogLevel = static_cast<uint8_t>(LogLevel::NONE);
  // Wait after shot end before capturing the post-drip weight.
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
};

struct CycleConfigSnapshot {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  bool timerOnly = false;
  bool canTareStartTimer = true;
  uint32_t scaleTimerStopExtraDelayMs = DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS;
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
  bool firstDropBeep = true;
  bool paddleReturnReminderBeep = true;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint8_t paddleMode = static_cast<uint8_t>(PaddleMode::NATURAL);
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  bool fastExtractionGuardEnabled = true;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBrewTimeMs = DEFAULT_MAX_BREW_TIME_MS;
  bool autoToManualGuardEnabled = true;
  uint8_t autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  uint32_t autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  float cupPresentWeightG = DEFAULT_CUP_PRESENT_WEIGHT_G;
  float cupRemovedWeightG = DEFAULT_CUP_REMOVED_WEIGHT_G;
  uint16_t autoToManualGuardSamplesDs[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS};
};

inline CycleConfigSnapshot snapshotConfig(const RuntimeConfig &config) {
  CycleConfigSnapshot snapshot;
  snapshot.revision = config.revision;
  snapshot.goalWeightG = config.goalWeightG;
  snapshot.weightOffsetG = config.weightOffsetG;
  snapshot.autoTare = config.autoTare;
  snapshot.timerOnly = config.timerOnly;
  snapshot.canTareStartTimer = config.canTareStartTimer;
  snapshot.scaleTimerStopExtraDelayMs = config.scaleTimerStopExtraDelayMs;
  snapshot.dripDelayMs = config.dripDelayMs;
  snapshot.firstDropBeep = config.firstDropBeep;
  snapshot.paddleReturnReminderBeep = config.paddleReturnReminderBeep;
  snapshot.paddleReturnReminderIntervalMs =
      config.paddleReturnReminderIntervalMs;
  snapshot.paddleReturnReminderMaxDurationMs =
      config.paddleReturnReminderMaxDurationMs;
  snapshot.paddleMode = config.paddleMode;
  snapshot.rinseGestureMs = config.rinseGestureMs;
  snapshot.rinseDurationMs = config.rinseDurationMs;
  snapshot.autoRetare = config.autoRetare;
  snapshot.retareWindowMs = config.retareWindowMs;
  snapshot.minimumCupWeightG = config.minimumCupWeightG;
  snapshot.retareStabilitySamples = config.retareStabilitySamples;
  snapshot.retareStabilityToleranceG = config.retareStabilityToleranceG;
  snapshot.retareStabilityMaxGapMs = config.retareStabilityMaxGapMs;
  snapshot.retareStabilityMinDurationMs = config.retareStabilityMinDurationMs;
  snapshot.bbwProtectionMs = config.bbwProtectionMs;
  snapshot.operationalWallMs = config.operationalWallMs;
  snapshot.fastExtractionGuardEnabled = config.fastExtractionGuardEnabled;
  snapshot.maxRecoveryWeightG = config.maxRecoveryWeightG;
  snapshot.minBrewTimeMs = config.minBrewTimeMs;
  snapshot.slowExtractionGuardEnabled = config.slowExtractionGuardEnabled;
  snapshot.minRecoveryWeightG = config.minRecoveryWeightG;
  snapshot.maxBrewTimeMs = config.maxBrewTimeMs;
  snapshot.autoToManualGuardEnabled = config.autoToManualGuardEnabled;
  snapshot.autoToManualGuardLimitMode = config.autoToManualGuardLimitMode;
  snapshot.autoToManualGuardManualLimitMs =
      config.autoToManualGuardManualLimitMs;
  snapshot.cupProtectionEnabled = config.cupProtectionEnabled;
  snapshot.stopIfCupRemoved = config.stopIfCupRemoved;
  snapshot.requireCupToStart = config.requireCupToStart;
  snapshot.cupPresentWeightG = config.cupPresentWeightG;
  snapshot.cupRemovedWeightG = config.cupRemovedWeightG;
  memcpy(snapshot.autoToManualGuardSamplesDs, config.autoToManualGuardSamplesDs,
         sizeof(snapshot.autoToManualGuardSamplesDs));
  return snapshot;
}

enum class ConfigValidationError : uint8_t {
  NONE,
  GOAL_WEIGHT,
  WEIGHT_OFFSET,
  RINSE_GESTURE,
  RINSE_DURATION,
  RETARE_WINDOW,
  MINIMUM_CUP_WEIGHT,
  RETARE_STABILITY_SAMPLES,
  RETARE_STABILITY_TOLERANCE,
  RETARE_STABILITY_MAX_GAP,
  RETARE_STABILITY_MIN_DURATION,
  RETARE_STABILITY_RELATION,
  BBW_PROTECTION_TIMEOUT,
  BBW_PROTECTION_RETARE_RELATION,
  OPERATIONAL_WALL,
  PADDLE_REMINDER_INTERVAL,
  PADDLE_REMINDER_MAX_DURATION,
  TIMING_RELATION,
  COMBINED_TARE_REQUIRES_AUTOTARE,
  SCALE_TIMER_STOP_EXTRA_DELAY,
  TIMEZONE_OFFSET,
  NTP_SERVER_PRESET,
  NTP_SERVER_CUSTOM,
  MAX_RECOVERY_WEIGHT,
  MIN_BREW_TIME,
  FAST_EXTRACTION_GUARD_RELATION,
  MIN_RECOVERY_WEIGHT,
  MAX_BREW_TIME,
  SLOW_EXTRACTION_GUARD_RELATION,
  AUTO_TO_MANUAL_GUARD_MODE,
  AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT,
  AUTO_TO_MANUAL_GUARD_BASELINE,
  WEIGHT_OFFSET_BASELINE,
  SCALE_MAC_CACHE_MODE,
  ALERT_OUTPUT_CHANNEL,
  EXTENDED_PULSE_RATE,
  SLOW_EXTENDED_PULSE_RATE,
  BOOKOO_CONNECT_BEEP_LEVEL,
  LAST_SHOT_COOLDOWN,
  DRIP_DELAY,
  RING_RETAIN_LOG_LEVEL,
  PADDLE_MODE,
  CUP_PRESENT_WEIGHT,
  CUP_REMOVED_WEIGHT
};

constexpr size_t MAX_SHOT_PRESETS = 8;
constexpr size_t SHOT_PRESET_NAME_CAPACITY = 24;
constexpr uint8_t FACTORY_PRESET_ID_SINGLE = 1;
constexpr uint8_t FACTORY_PRESET_ID_DOUBLE = 2;
constexpr float FACTORY_SINGLE_WEIGHT_OFFSET_G = 0.5f;
constexpr uint8_t FACTORY_SINGLE_GOAL_WEIGHT_G = 18;
constexpr float FACTORY_SINGLE_MAX_RECOVERY_WEIGHT_G = 20.0f;
constexpr uint32_t FACTORY_SINGLE_MIN_BREW_TIME_MS = 28000;
constexpr float FACTORY_SINGLE_MIN_RECOVERY_WEIGHT_G = 16.0f;
constexpr uint32_t FACTORY_SINGLE_MAX_BREW_TIME_MS = 44000;

inline void repairSlowExtractionGuard(RuntimeConfig &runtime) {
  if (!runtime.slowExtractionGuardEnabled) {
    return;
  }
  if (!isfinite(runtime.minRecoveryWeightG) ||
      runtime.minRecoveryWeightG < MIN_MIN_RECOVERY_WEIGHT_G ||
      runtime.minRecoveryWeightG > MAX_MIN_RECOVERY_WEIGHT_G ||
      runtime.minRecoveryWeightG >= static_cast<float>(runtime.goalWeightG)) {
    const float repaired = static_cast<float>(runtime.goalWeightG) - 1.0f;
    if (repaired >= MIN_MIN_RECOVERY_WEIGHT_G &&
        repaired <= MAX_MIN_RECOVERY_WEIGHT_G &&
        repaired < static_cast<float>(runtime.goalWeightG)) {
      runtime.minRecoveryWeightG = repaired;
    } else {
      runtime.slowExtractionGuardEnabled = false;
    }
  }
  if (runtime.maxBrewTimeMs >= runtime.operationalWallMs) {
    runtime.maxBrewTimeMs = runtime.operationalWallMs > 1000
                                ? runtime.operationalWallMs - 1000
                                : DEFAULT_MAX_BREW_TIME_MS;
  }
  if (runtime.maxBrewTimeMs < runtime.bbwProtectionMs) {
    runtime.maxBrewTimeMs = runtime.bbwProtectionMs;
  }
  if (runtime.fastExtractionGuardEnabled &&
      runtime.maxBrewTimeMs <= runtime.minBrewTimeMs) {
    const uint32_t repaired = runtime.minBrewTimeMs + 1000;
    if (repaired < runtime.operationalWallMs &&
        repaired <= MAX_MAX_BREW_TIME_MS) {
      runtime.maxBrewTimeMs = repaired;
    } else {
      runtime.slowExtractionGuardEnabled = false;
    }
  }
  if (runtime.slowExtractionGuardEnabled &&
      (!isfinite(runtime.minRecoveryWeightG) ||
       runtime.minRecoveryWeightG < MIN_MIN_RECOVERY_WEIGHT_G ||
       runtime.minRecoveryWeightG > MAX_MIN_RECOVERY_WEIGHT_G ||
       runtime.minRecoveryWeightG >= static_cast<float>(runtime.goalWeightG) ||
       runtime.maxBrewTimeMs < MIN_MAX_BREW_TIME_MS ||
       runtime.maxBrewTimeMs > MAX_MAX_BREW_TIME_MS ||
       runtime.maxBrewTimeMs >= runtime.operationalWallMs ||
       runtime.maxBrewTimeMs < runtime.bbwProtectionMs ||
       (runtime.fastExtractionGuardEnabled &&
        runtime.maxBrewTimeMs <= runtime.minBrewTimeMs))) {
    runtime.slowExtractionGuardEnabled = false;
  }
}

// NVS dual-slot budget headroom for PersistedSettings including the preset bank.
constexpr size_t PERSISTED_SETTINGS_NVS_BUDGET = 3072;

enum class PresetAction : uint8_t {
  APPLY = 0,
  SAVE = 1,
  CREATE = 2,
  DELETE = 3,
  DUPLICATE = 4,
  RENAME = 5,
  RESTORE_FACTORY_VALUES = 6
};

struct ShotPreset {
  uint8_t id = 0;
  char name[SHOT_PRESET_NAME_CAPACITY] = {};
  bool isFactory = false;
  bool brewByWeight = true;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  float weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool fastExtractionGuardEnabled = true;
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBrewTimeMs = DEFAULT_MIN_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBrewTimeMs = DEFAULT_MAX_BREW_TIME_MS;
  bool autoToManualGuardEnabled = true;
  uint8_t autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  uint32_t autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  uint32_t autoToManualGuardBaselineMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  float cupPresentWeightG = DEFAULT_CUP_PRESENT_WEIGHT_G;
  float cupRemovedWeightG = DEFAULT_CUP_REMOVED_WEIGHT_G;
  uint16_t autoToManualGuardSamplesDs[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS};
};

struct ShotPresetBank {
  uint8_t count = 0;
  uint8_t activeId = 0;
  uint8_t nextId = 3;
  ShotPreset presets[MAX_SHOT_PRESETS] = {};
};

static_assert(sizeof(ShotPreset) <= 136, "ShotPreset too large");
static_assert(sizeof(ShotPresetBank) <= 1100, "ShotPresetBank too large");

inline uint32_t effectiveRetareWindowMs(const RuntimeConfig &config) {
  return config.autoRetare ? config.retareWindowMs : 0U;
}

inline uint32_t minimumBbwProtectionMs(const RuntimeConfig &config) {
  return effectiveRetareWindowMs(config) + MIN_BBW_PROTECTION_AFTER_RETARE_MS;
}

inline uint32_t effectiveRetareWindowMs(const CycleConfigSnapshot &config) {
  return config.autoRetare ? config.retareWindowMs : 0U;
}

inline uint32_t minimumBbwProtectionMs(
    const CycleConfigSnapshot &config) {
  return effectiveRetareWindowMs(config) + MIN_BBW_PROTECTION_AFTER_RETARE_MS;
}

inline ConfigValidationError validateRuntimeConfig(
    const RuntimeConfig &config) {
  if (config.goalWeightG < MIN_GOAL_WEIGHT_G ||
      config.goalWeightG > MAX_GOAL_WEIGHT_G) {
    return ConfigValidationError::GOAL_WEIGHT;
  }
  if (!isfinite(config.weightOffsetG) || config.weightOffsetG < 0.0f ||
      config.weightOffsetG > MAX_OFFSET_G) {
    return ConfigValidationError::WEIGHT_OFFSET;
  }
  if (!isfinite(config.weightOffsetBaselineG) ||
      config.weightOffsetBaselineG < 0.0f ||
      config.weightOffsetBaselineG > MAX_OFFSET_G) {
    return ConfigValidationError::WEIGHT_OFFSET_BASELINE;
  }
  if (config.rinseGestureMs < 100 || config.rinseGestureMs > 5000) {
    return ConfigValidationError::RINSE_GESTURE;
  }
  if (config.rinseDurationMs < 500 || config.rinseDurationMs > 10000) {
    return ConfigValidationError::RINSE_DURATION;
  }
  if (config.retareWindowMs < MIN_RETARE_WINDOW_MS ||
      config.retareWindowMs > MAX_RETARE_WINDOW_MS) {
    return ConfigValidationError::RETARE_WINDOW;
  }
  if (!isfinite(config.minimumCupWeightG) ||
      config.minimumCupWeightG < MIN_MINIMUM_CUP_WEIGHT_G ||
      config.minimumCupWeightG > MAX_MINIMUM_CUP_WEIGHT_G) {
    return ConfigValidationError::MINIMUM_CUP_WEIGHT;
  }
  if (config.retareStabilitySamples < MIN_RETARE_STABILITY_SAMPLES ||
      config.retareStabilitySamples > MAX_RETARE_STABILITY_SAMPLES) {
    return ConfigValidationError::RETARE_STABILITY_SAMPLES;
  }
  if (!isfinite(config.retareStabilityToleranceG) ||
      config.retareStabilityToleranceG < MIN_RETARE_STABILITY_TOLERANCE_G ||
      config.retareStabilityToleranceG > MAX_RETARE_STABILITY_TOLERANCE_G) {
    return ConfigValidationError::RETARE_STABILITY_TOLERANCE;
  }
  if (config.retareStabilityMaxGapMs < MIN_RETARE_STABILITY_MAX_GAP_MS ||
      config.retareStabilityMaxGapMs > MAX_RETARE_STABILITY_MAX_GAP_MS) {
    return ConfigValidationError::RETARE_STABILITY_MAX_GAP;
  }
  if (config.retareStabilityMinDurationMs <
          MIN_RETARE_STABILITY_MIN_DURATION_MS ||
      config.retareStabilityMinDurationMs >
          MAX_RETARE_STABILITY_MIN_DURATION_MS) {
    return ConfigValidationError::RETARE_STABILITY_MIN_DURATION;
  }
  if (config.retareStabilityMinDurationMs > config.retareWindowMs) {
    return ConfigValidationError::RETARE_STABILITY_RELATION;
  }
  if (config.retareStabilityMinDurationMs > 0U &&
      config.retareStabilityMinDurationMs >
          static_cast<uint32_t>(config.retareStabilitySamples) *
              config.retareStabilityMaxGapMs) {
    return ConfigValidationError::RETARE_STABILITY_RELATION;
  }
  if (config.bbwProtectionMs < MIN_BBW_PROTECTION_MS ||
      config.bbwProtectionMs > MAX_BBW_PROTECTION_MS) {
    return ConfigValidationError::BBW_PROTECTION_TIMEOUT;
  }
  if (config.bbwProtectionMs <
      minimumBbwProtectionMs(config)) {
    return ConfigValidationError::BBW_PROTECTION_RETARE_RELATION;
  }
  if (config.operationalWallMs < 5000 ||
      config.operationalWallMs > HARD_MAX_CN9_CLOSED_MS) {
    return ConfigValidationError::OPERATIONAL_WALL;
  }
  if (config.paddleReturnReminderIntervalMs <
          MIN_PADDLE_RETURN_REMINDER_INTERVAL_MS ||
      config.paddleReturnReminderIntervalMs >
          MAX_PADDLE_RETURN_REMINDER_INTERVAL_MS) {
    return ConfigValidationError::PADDLE_REMINDER_INTERVAL;
  }
  if (config.paddleReturnReminderMaxDurationMs <
          MIN_PADDLE_RETURN_REMINDER_MAX_DURATION_MS ||
      config.paddleReturnReminderMaxDurationMs >
          MAX_PADDLE_RETURN_REMINDER_MAX_DURATION_MS ||
      config.paddleReturnReminderMaxDurationMs <
          config.paddleReturnReminderIntervalMs) {
    return ConfigValidationError::PADDLE_REMINDER_MAX_DURATION;
  }
  // Retare and BBW protection run in parallel from paddle ON — each alone
  // must fit under the CN9 wall (do not sum them).
  if (!(config.rinseGestureMs < config.operationalWallMs) ||
      config.rinseDurationMs > config.operationalWallMs ||
      config.retareWindowMs > config.operationalWallMs ||
      config.bbwProtectionMs > config.operationalWallMs) {
    return ConfigValidationError::TIMING_RELATION;
  }
  if (config.canTareStartTimer && !config.autoTare) {
    return ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE;
  }
  if (config.scaleTimerStopExtraDelayMs < MIN_SCALE_TIMER_STOP_EXTRA_DELAY_MS ||
      config.scaleTimerStopExtraDelayMs > MAX_SCALE_TIMER_STOP_EXTRA_DELAY_MS) {
    return ConfigValidationError::SCALE_TIMER_STOP_EXTRA_DELAY;
  }
  if (config.timezoneOffsetMinutes < MIN_TIMEZONE_OFFSET_MINUTES ||
      config.timezoneOffsetMinutes > MAX_TIMEZONE_OFFSET_MINUTES) {
    return ConfigValidationError::TIMEZONE_OFFSET;
  }
  if (config.ntpServerPreset >
      static_cast<uint8_t>(NtpServerPreset::NIST)) {
    return ConfigValidationError::NTP_SERVER_PRESET;
  }
  if (config.ntpServerCustom[0] != '\0' &&
      !validNtpHostname(config.ntpServerCustom)) {
    return ConfigValidationError::NTP_SERVER_CUSTOM;
  }
  if (config.autoToManualGuardLimitMode >
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO)) {
    return ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE;
  }
  if (config.autoToManualGuardManualLimitMs <
          MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS ||
      config.autoToManualGuardManualLimitMs > config.operationalWallMs) {
    return ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT;
  }
  if (config.autoToManualGuardBaselineMs < MIN_AUTO_TO_MANUAL_GUARD_LIMIT_MS ||
      config.autoToManualGuardBaselineMs > config.operationalWallMs) {
    return ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE;
  }
  if (!validScaleMacCacheMode(config.scaleMacCacheMode)) {
    return ConfigValidationError::SCALE_MAC_CACHE_MODE;
  }
  if (!validAlertOutputChannel(config.alertOutputChannel)) {
    return ConfigValidationError::ALERT_OUTPUT_CHANNEL;
  }
  if (!validExtendedPulseRate(config.buzzerExtendedPulseRate)) {
    return ConfigValidationError::EXTENDED_PULSE_RATE;
  }
  if (!validExtendedPulseRate(config.buzzerSlowExtendedPulseRate)) {
    return ConfigValidationError::SLOW_EXTENDED_PULSE_RATE;
  }
  if (config.bookooConnectBeepLevel > BOOKOO_BEEP_LEVEL_MAX) {
    return ConfigValidationError::BOOKOO_CONNECT_BEEP_LEVEL;
  }
  if (config.lastShotCooldownMs < MIN_LAST_SHOT_COOLDOWN_MS ||
      config.lastShotCooldownMs > MAX_LAST_SHOT_COOLDOWN_MS) {
    return ConfigValidationError::LAST_SHOT_COOLDOWN;
  }
  if (config.dripDelayMs < MIN_DRIP_DELAY_MS ||
      config.dripDelayMs > MAX_DRIP_DELAY_MS) {
    return ConfigValidationError::DRIP_DELAY;
  }
  if (config.ringRetainLogLevel >
      static_cast<uint8_t>(LogLevel::NONE)) {
    return ConfigValidationError::RING_RETAIN_LOG_LEVEL;
  }
  if (!validPaddleMode(config.paddleMode)) {
    return ConfigValidationError::PADDLE_MODE;
  }
  if (!isfinite(config.cupPresentWeightG) ||
      config.cupPresentWeightG < MIN_CUP_PRESENT_WEIGHT_G ||
      config.cupPresentWeightG > MAX_CUP_PRESENT_WEIGHT_G) {
    return ConfigValidationError::CUP_PRESENT_WEIGHT;
  }
  if (!isfinite(config.cupRemovedWeightG) ||
      config.cupRemovedWeightG < MIN_CUP_REMOVED_WEIGHT_G ||
      config.cupRemovedWeightG > MAX_CUP_REMOVED_WEIGHT_G) {
    return ConfigValidationError::CUP_REMOVED_WEIGHT;
  }
  if (config.fastExtractionGuardEnabled) {
    if (!isfinite(config.maxRecoveryWeightG) ||
        config.maxRecoveryWeightG < MIN_MAX_RECOVERY_WEIGHT_G ||
        config.maxRecoveryWeightG > MAX_MAX_RECOVERY_WEIGHT_G) {
      return ConfigValidationError::MAX_RECOVERY_WEIGHT;
    }
    if (config.minBrewTimeMs < MIN_MIN_BREW_TIME_MS ||
        config.minBrewTimeMs > MAX_MIN_BREW_TIME_MS) {
      return ConfigValidationError::MIN_BREW_TIME;
    }
    if (config.maxRecoveryWeightG <= static_cast<float>(config.goalWeightG) ||
        config.minBrewTimeMs >= config.operationalWallMs ||
        config.minBrewTimeMs < config.bbwProtectionMs) {
      return ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION;
    }
  }
  if (config.slowExtractionGuardEnabled) {
    if (!isfinite(config.minRecoveryWeightG) ||
        config.minRecoveryWeightG < MIN_MIN_RECOVERY_WEIGHT_G ||
        config.minRecoveryWeightG > MAX_MIN_RECOVERY_WEIGHT_G) {
      return ConfigValidationError::MIN_RECOVERY_WEIGHT;
    }
    if (config.maxBrewTimeMs < MIN_MAX_BREW_TIME_MS ||
        config.maxBrewTimeMs > MAX_MAX_BREW_TIME_MS) {
      return ConfigValidationError::MAX_BREW_TIME;
    }
    if (config.minRecoveryWeightG >= static_cast<float>(config.goalWeightG) ||
        config.maxBrewTimeMs >= config.operationalWallMs ||
        config.maxBrewTimeMs < config.bbwProtectionMs ||
        (config.fastExtractionGuardEnabled &&
         config.maxBrewTimeMs <= config.minBrewTimeMs)) {
      return ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION;
    }
  }
  return ConfigValidationError::NONE;
}

inline const char *configValidationErrorName(ConfigValidationError error) {
  switch (error) {
    case ConfigValidationError::NONE: return "none";
    case ConfigValidationError::GOAL_WEIGHT: return "goalWeightG";
    case ConfigValidationError::WEIGHT_OFFSET: return "weightOffsetG";
    case ConfigValidationError::RINSE_GESTURE: return "rinseGestureMs";
    case ConfigValidationError::RINSE_DURATION: return "rinseDurationMs";
    case ConfigValidationError::RETARE_WINDOW: return "retareWindowMs";
    case ConfigValidationError::MINIMUM_CUP_WEIGHT: return "minimumCupWeightG";
    case ConfigValidationError::RETARE_STABILITY_SAMPLES:
      return "retareStabilitySamples";
    case ConfigValidationError::RETARE_STABILITY_TOLERANCE:
      return "retareStabilityToleranceG";
    case ConfigValidationError::RETARE_STABILITY_MAX_GAP:
      return "retareStabilityMaxGapMs";
    case ConfigValidationError::RETARE_STABILITY_MIN_DURATION:
      return "retareStabilityMinDurationMs";
    case ConfigValidationError::RETARE_STABILITY_RELATION:
      return "retareStabilityRelation";
    case ConfigValidationError::BBW_PROTECTION_TIMEOUT:
      return "bbwProtectionMs";
    case ConfigValidationError::BBW_PROTECTION_RETARE_RELATION:
      return "bbwProtectionRetareRelation";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "operationalWallMs";
    case ConfigValidationError::PADDLE_REMINDER_INTERVAL:
      return "paddleReturnReminderIntervalMs";
    case ConfigValidationError::PADDLE_REMINDER_MAX_DURATION:
      return "paddleReturnReminderMaxDurationMs";
    case ConfigValidationError::TIMING_RELATION: return "timingRelation";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "canTareStartTimer";
    case ConfigValidationError::SCALE_TIMER_STOP_EXTRA_DELAY:
      return "scaleTimerStopExtraDelayMs";
    case ConfigValidationError::TIMEZONE_OFFSET:
      return "timezoneOffsetMinutes";
    case ConfigValidationError::NTP_SERVER_PRESET:
      return "ntpServerPreset";
    case ConfigValidationError::NTP_SERVER_CUSTOM:
      return "ntpServerCustom";
    case ConfigValidationError::MAX_RECOVERY_WEIGHT:
      return "maxRecoveryWeightG";
    case ConfigValidationError::MIN_BREW_TIME:
      return "minBrewTimeMs";
    case ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION:
      return "fastExtractionGuardRelation";
    case ConfigValidationError::MIN_RECOVERY_WEIGHT:
      return "minRecoveryWeightG";
    case ConfigValidationError::MAX_BREW_TIME:
      return "maxBrewTimeMs";
    case ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION:
      return "slowExtractionGuardRelation";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE:
      return "autoToManualGuardLimitMode";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT:
      return "autoToManualGuardManualLimitMs";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE:
      return "autoToManualGuardBaselineMs";
    case ConfigValidationError::WEIGHT_OFFSET_BASELINE:
      return "weightOffsetBaselineG";
    case ConfigValidationError::SCALE_MAC_CACHE_MODE:
      return "scaleMacCacheMode";
    case ConfigValidationError::ALERT_OUTPUT_CHANNEL:
      return "alertOutputChannel";
    case ConfigValidationError::EXTENDED_PULSE_RATE:
      return "buzzerExtendedPulseRate";
    case ConfigValidationError::SLOW_EXTENDED_PULSE_RATE:
      return "buzzerSlowExtendedPulseRate";
    case ConfigValidationError::BOOKOO_CONNECT_BEEP_LEVEL:
      return "bookooConnectBeepLevel";
    case ConfigValidationError::LAST_SHOT_COOLDOWN:
      return "lastShotCooldownMs";
    case ConfigValidationError::DRIP_DELAY:
      return "dripDelayMs";
    case ConfigValidationError::RING_RETAIN_LOG_LEVEL:
      return "ringRetainLogLevel";
    case ConfigValidationError::PADDLE_MODE:
      return "paddleMode";
    case ConfigValidationError::CUP_PRESENT_WEIGHT:
      return "cupPresentWeightG";
    case ConfigValidationError::CUP_REMOVED_WEIGHT:
      return "cupRemovedWeightG";
  }
  return "unknown";
}

inline bool boundedCString(const char *value, size_t capacity,
                           size_t *length = nullptr) {
  if (value == nullptr || capacity == 0) {
    return false;
  }
  size_t count = 0;
  while (count < capacity && value[count] != '\0') {
    ++count;
  }
  if (count == capacity) {
    return false;
  }
  if (length != nullptr) {
    *length = count;
  }
  return true;
}

inline bool validWifiSsid(const char *ssid) {
  size_t length = 0;
  return boundedCString(ssid, WIFI_SSID_CAPACITY, &length) && length >= 1 &&
         length <= 32;
}

inline bool validWifiPassword(const char *password, bool openNetwork) {
  size_t length = 0;
  if (!boundedCString(password, WIFI_PASSWORD_CAPACITY, &length)) {
    return false;
  }
  return openNetwork ? length == 0 : length >= 8 && length <= 63;
}

inline bool shouldReuseSavedWifiCredentials(const char *ssid,
                                            const char *password,
                                            bool openNetwork,
                                            bool staConfigured,
                                            const char *savedSsid,
                                            bool savedOpen) {
  return password != nullptr && password[0] == '\0' && staConfigured &&
         validWifiSsid(ssid) && validWifiSsid(savedSsid) &&
         strcmp(ssid, savedSsid) == 0 && openNetwork == savedOpen;
}

inline bool validAccessPointPassword(const char *password) {
  return validWifiPassword(password, false);
}

inline bool validBleMacNibble(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

// Empty = no preferred scale. Otherwise canonical "AA:BB:CC:DD:EE:FF".
inline bool validPreferredScaleMac(const char *mac) {
  if (mac == nullptr) {
    return false;
  }
  const size_t length = strnlen(mac, PREFERRED_SCALE_MAC_CAPACITY);
  if (length == 0) {
    return mac[0] == '\0';
  }
  if (length != 17 || mac[17] != '\0') {
    return false;
  }
  for (size_t index = 0; index < 17; ++index) {
    if ((index % 3) == 2) {
      if (mac[index] != ':') {
        return false;
      }
    } else if (!validBleMacNibble(mac[index])) {
      return false;
    }
  }
  return true;
}

// Empty name is allowed. Otherwise printable ASCII without quotes/controls.
inline bool validPreferredScaleName(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const size_t length = strnlen(name, PREFERRED_SCALE_NAME_CAPACITY);
  if (length >= PREFERRED_SCALE_NAME_CAPACITY) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const unsigned char c = static_cast<unsigned char>(name[index]);
    if (c < 0x20 || c > 0x7e || c == '"' || c == '\\') {
      return false;
    }
  }
  return true;
}

inline bool validScaleHistoryEntries(const ScaleHistoryEntry *entries) {
  if (entries == nullptr) {
    return false;
  }
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (!validPreferredScaleMac(entries[i].mac) ||
        !validPreferredScaleName(entries[i].name)) {
      return false;
    }
  }
  return true;
}

inline char preferredScaleMacUpper(char c) {
  if (c >= 'a' && c <= 'z') {
    return static_cast<char>(c - ('a' - 'A'));
  }
  return c;
}

// Case-insensitive BLE MAC compare (AA:BB:… vs aa:bb:…).
inline bool preferredScaleMacEqual(const char *left, const char *right) {
  if (left == nullptr || right == nullptr) {
    return false;
  }
  for (size_t i = 0; i < PREFERRED_SCALE_MAC_CAPACITY; ++i) {
    const char a = preferredScaleMacUpper(left[i]);
    const char b = preferredScaleMacUpper(right[i]);
    if (a != b) {
      return false;
    }
    if (a == '\0') {
      return true;
    }
  }
  return true;
}

inline void canonicalizePreferredScaleMac(char *mac, size_t capacity) {
  if (mac == nullptr || capacity == 0) {
    return;
  }
  for (size_t i = 0; i + 1 < capacity && mac[i] != '\0'; ++i) {
    mac[i] = preferredScaleMacUpper(mac[i]);
  }
}

inline size_t scaleHistoryOccupiedCount(const ScaleHistoryEntry *entries) {
  size_t count = 0;
  if (entries == nullptr) {
    return 0;
  }
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (entries[i].mac[0] != '\0') {
      ++count;
    }
  }
  return count;
}

// Upsert by MAC (case-insensitive). Stores canonical uppercase MAC.
// Returns true if the table changed. Advances *seqCounter.
inline bool upsertScaleHistory(ScaleHistoryEntry *entries, uint32_t &seqCounter,
                               const char *mac, const char *name) {
  if (entries == nullptr || mac == nullptr || !validPreferredScaleMac(mac) ||
      mac[0] == '\0') {
    return false;
  }
  char canonicalMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  strncpy(canonicalMac, mac, PREFERRED_SCALE_MAC_CAPACITY - 1);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char safeName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name)) {
    strncpy(safeName, name, PREFERRED_SCALE_NAME_CAPACITY - 1);
  }
  ++seqCounter;
  if (seqCounter == 0) {
    seqCounter = 1;
  }
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (preferredScaleMacEqual(entries[i].mac, canonicalMac)) {
      bool changed = false;
      if (strncmp(entries[i].mac, canonicalMac, PREFERRED_SCALE_MAC_CAPACITY) !=
          0) {
        memcpy(entries[i].mac, canonicalMac, sizeof(entries[i].mac));
        changed = true;
      }
      if (strncmp(entries[i].name, safeName, PREFERRED_SCALE_NAME_CAPACITY) !=
          0) {
        memcpy(entries[i].name, safeName, sizeof(entries[i].name));
        changed = true;
      }
      entries[i].lastSeenSeq = seqCounter;
      return changed;
    }
  }
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (entries[i].mac[0] == '\0') {
      memcpy(entries[i].mac, canonicalMac, sizeof(entries[i].mac));
      memcpy(entries[i].name, safeName, sizeof(entries[i].name));
      entries[i].lastSeenSeq = seqCounter;
      return true;
    }
  }
  size_t victim = 0;
  uint32_t oldest = entries[0].lastSeenSeq;
  for (size_t i = 1; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (entries[i].lastSeenSeq < oldest) {
      oldest = entries[i].lastSeenSeq;
      victim = i;
    }
  }
  memcpy(entries[victim].mac, canonicalMac, sizeof(entries[victim].mac));
  memcpy(entries[victim].name, safeName, sizeof(entries[victim].name));
  entries[victim].lastSeenSeq = seqCounter;
  return true;
}

inline void seedScaleHistoryFromPreferred(ScaleHistoryEntry *entries,
                                          uint32_t &seqCounter,
                                          const char *mac, const char *name) {
  if (entries == nullptr || scaleHistoryOccupiedCount(entries) > 0) {
    return;
  }
  if (mac == nullptr || mac[0] == '\0' || !validPreferredScaleMac(mac)) {
    return;
  }
  upsertScaleHistory(entries, seqCounter, mac, name);
}

inline bool findScaleHistoryName(const ScaleHistoryEntry *entries,
                                 const char *mac, char *nameOut,
                                 size_t nameOutCapacity) {
  if (entries == nullptr || mac == nullptr || nameOut == nullptr ||
      nameOutCapacity == 0) {
    return false;
  }
  nameOut[0] = '\0';
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (preferredScaleMacEqual(entries[i].mac, mac)) {
      strncpy(nameOut, entries[i].name, nameOutCapacity - 1);
      nameOut[nameOutCapacity - 1] = '\0';
      return true;
    }
  }
  return false;
}

enum class StaIpMode : uint8_t { DHCP = 0, STATIC = 1 };

enum class StaConfigState : uint8_t { CONFIRMED = 0, PENDING = 1 };

inline bool ipv4IsZero(const uint8_t ip[4]) {
  return ip != nullptr && ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

inline uint32_t ipv4ToHostOrder(const uint8_t ip[4]) {
  if (ip == nullptr) {
    return 0;
  }
  return (static_cast<uint32_t>(ip[0]) << 24) |
         (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) |
         static_cast<uint32_t>(ip[3]);
}

inline void formatIpv4(const uint8_t ip[4], char output[16]) {
  if (output == nullptr) {
    return;
  }
  if (ip == nullptr) {
    output[0] = '\0';
    return;
  }
  snprintf(output, 16, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]),
           static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
           static_cast<unsigned>(ip[3]));
}

inline bool parseIpv4(const char *text, uint8_t output[4]) {
  if (text == nullptr || output == nullptr) {
    return false;
  }
  unsigned parts[4] = {};
  char trailer = '\0';
  if (sscanf(text, "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3],
             &trailer) != 4) {
    return false;
  }
  for (size_t index = 0; index < 4; ++index) {
    if (parts[index] > 255U) {
      return false;
    }
    output[index] = static_cast<uint8_t>(parts[index]);
  }
  return true;
}

inline bool validIpv4Netmask(const uint8_t netmask[4]) {
  if (netmask == nullptr || ipv4IsZero(netmask)) {
    return false;
  }
  const uint32_t mask = ipv4ToHostOrder(netmask);
  // Valid netmasks are a contiguous run of 1-bits followed by 0-bits.
  const uint32_t inverted = ~mask;
  return (inverted & (inverted + 1U)) == 0U;
}

inline bool ipv4SameSubnet(const uint8_t left[4], const uint8_t right[4],
                           const uint8_t netmask[4]) {
  if (left == nullptr || right == nullptr || netmask == nullptr) {
    return false;
  }
  const uint32_t mask = ipv4ToHostOrder(netmask);
  return (ipv4ToHostOrder(left) & mask) == (ipv4ToHostOrder(right) & mask);
}

inline bool ipv4InSoftApSubnet(const uint8_t ip[4]) {
  return ip != nullptr && ip[0] == 192 && ip[1] == 168 && ip[2] == 4;
}

inline bool validStaIpMode(uint8_t mode) {
  return mode == static_cast<uint8_t>(StaIpMode::DHCP) ||
         mode == static_cast<uint8_t>(StaIpMode::STATIC);
}

inline bool validStaConfigState(uint8_t state) {
  return state == static_cast<uint8_t>(StaConfigState::CONFIRMED) ||
         state == static_cast<uint8_t>(StaConfigState::PENDING);
}

inline bool validStaAddressConfig(uint8_t mode, const uint8_t ip[4],
                                  const uint8_t netmask[4],
                                  const uint8_t gateway[4],
                                  const uint8_t dns1[4],
                                  const uint8_t dns2[4]) {
  if (!validStaIpMode(mode)) {
    return false;
  }
  if (mode == static_cast<uint8_t>(StaIpMode::DHCP)) {
    return ipv4IsZero(ip) && ipv4IsZero(netmask) && ipv4IsZero(gateway) &&
           ipv4IsZero(dns1) && ipv4IsZero(dns2);
  }
  if (ipv4IsZero(ip) || ipv4IsZero(gateway) || ipv4IsZero(dns1) ||
      !validIpv4Netmask(netmask) || ipv4InSoftApSubnet(ip) ||
      ipv4InSoftApSubnet(gateway) || memcmp(ip, gateway, 4) == 0 ||
      !ipv4SameSubnet(ip, gateway, netmask)) {
    return false;
  }
  // Host bits must not be all-zero (network) or all-one (broadcast).
  const uint32_t mask = ipv4ToHostOrder(netmask);
  const uint32_t host = ipv4ToHostOrder(ip) & ~mask;
  if (host == 0U || host == ~mask) {
    return false;
  }
  return dns2 != nullptr;
}

inline const char *staIpModeName(uint8_t mode) {
  return mode == static_cast<uint8_t>(StaIpMode::STATIC) ? "static" : "dhcp";
}

inline const char *staConfigStateName(uint8_t state) {
  return state == static_cast<uint8_t>(StaConfigState::PENDING) ? "PENDING"
                                                               : "CONFIRMED";
}

enum class WebCommandType : uint8_t {
  PADDLE_ON,
  PADDLE_OFF,
  RINSE,
  STOP,
  STOP_HEARTBEAT,
  APPLY_CONFIG,
  RESET_WEIGHT_OFFSET,
  RESET_AUTO_TO_MANUAL_GUARD_SAMPLES,
  PRESET_OP,
  SAVE_NETWORK,
  FORGET_NETWORK,
  CHANGE_AP_PASSWORD,
  RESET_AP_PASSWORD,
  RESTART,
  RESET_NETWORK_AP,
  FACTORY_RESET,
  CLEAR_SHOT_LOG,
  CLEAR_PREFERRED_SCALE,
  SELECT_PREFERRED_SCALE,
  PERSIST_RUNTIME,
  START_WIFI_SCAN,
  BUZZER_TEST,
  BOOKOO_DEBUG,
  WIFI_CONNECT,
  WIFI_DISCONNECT,
  WIFI_RESTART,
  AP_START,
  AP_STOP,
  WEBUI_START,
  WEBUI_STOP,
  WEBUI_RESTART,
  BLE_COMPAT_ENABLE,
  BLE_COMPAT_DISABLE,
  MAINTENANCE_COMPLETE
};

inline const char *webCommandTypeName(WebCommandType type) {
  switch (type) {
    case WebCommandType::PADDLE_ON: return "paddle web on";
    case WebCommandType::PADDLE_OFF: return "paddle web off";
    case WebCommandType::RINSE: return "rinse web";
    case WebCommandType::STOP: return "web stop";
    case WebCommandType::STOP_HEARTBEAT:
      return "web heartbeat stop";
    case WebCommandType::APPLY_CONFIG: return "save workflow";
    case WebCommandType::RESET_WEIGHT_OFFSET:
      return "reset learned weight offset";
    case WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES:
      return "reset auto-to-manual guard samples";
    case WebCommandType::PRESET_OP: return "preset operation";
    case WebCommandType::SAVE_NETWORK: return "save STA network";
    case WebCommandType::FORGET_NETWORK: return "forget STA network";
    case WebCommandType::CHANGE_AP_PASSWORD: return "change AP password";
    case WebCommandType::RESET_AP_PASSWORD:
      return "restore AP password";
    case WebCommandType::RESTART: return "restart";
    case WebCommandType::RESET_NETWORK_AP: return "recover network/AP";
    case WebCommandType::FACTORY_RESET: return "restore factory settings";
    case WebCommandType::CLEAR_SHOT_LOG: return "clear shot history";
    case WebCommandType::CLEAR_PREFERRED_SCALE:
      return "forget paired scale";
    case WebCommandType::SELECT_PREFERRED_SCALE:
      return "select preferred scale";
    case WebCommandType::PERSIST_RUNTIME: return "persist workflow";
    case WebCommandType::START_WIFI_SCAN: return "scan Wi-Fi networks";
    case WebCommandType::BUZZER_TEST: return "buzzer test";
    case WebCommandType::BOOKOO_DEBUG: return "bookoo debug";
    case WebCommandType::WIFI_CONNECT: return "Wi-Fi connect";
    case WebCommandType::WIFI_DISCONNECT: return "Wi-Fi disconnect";
    case WebCommandType::WIFI_RESTART: return "Wi-Fi restart";
    case WebCommandType::AP_START: return "start access point";
    case WebCommandType::AP_STOP: return "stop access point";
    case WebCommandType::WEBUI_START: return "start Web UI";
    case WebCommandType::WEBUI_STOP: return "stop Web UI";
    case WebCommandType::WEBUI_RESTART: return "restart Web UI";
    case WebCommandType::BLE_COMPAT_ENABLE: return "enable BLE Companion";
    case WebCommandType::BLE_COMPAT_DISABLE: return "disable BLE Companion";
    case WebCommandType::MAINTENANCE_COMPLETE:
      return "maintenance result";
  }
  return "unknown web command";
}

inline bool isCliNetworkAction(WebCommandType type) {
  switch (type) {
    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
      return true;
    default:
      return false;
  }
}

inline LogLevel serialLogLevelFromRuntime(const RuntimeConfig &config) {
  if (!config.serialDebugOutput) {
    return LogLevel::NONE;
  }
  if (config.ringRetainLogLevel == static_cast<uint8_t>(LogLevel::DEBUG)) {
    return LogLevel::DEBUG;
  }
  return LogLevel::INFO;
}

enum class CommandResultState : uint8_t {
  NONE,
  QUEUED,
  RESERVED,
  APPLIED,
  PERSISTED,
  FAILED,
  CANCELED
};

struct WebCommand {
  WebCommandType type = WebCommandType::STOP;
  uint32_t requestId = 0;
  // Set only by the network task after an explicitly confirmed WebUI unlock.
  // It never changes relay safety; it only permits this queued WebUI command
  // to bypass the normal configuration-state gate.
  bool unsafeWebUiOverride = false;
  uint32_t maintenanceLeaseId = 0;
  RuntimeConfig config = {};
  // PRESET_OP payload (keep small — no full bank on the queue element).
  uint8_t presetAction = 0;
  uint8_t presetId = 0;
  char presetName[24] = {};
  bool persistPresets = false;
  BuzzerPattern buzzerPattern = BuzzerPattern::NONE;
  BookooDebugAction bookooDebugAction = BookooDebugAction::START;
  uint8_t bookooBeepLevel = 0;
  char ssid[WIFI_SSID_CAPACITY] = {};
  char password[WIFI_PASSWORD_CAPACITY] = {};
  bool openNetwork = false;
  uint8_t staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t staIp[4] = {};
  uint8_t staNetmask[4] = {};
  uint8_t staGateway[4] = {};
  uint8_t staDns1[4] = {};
  uint8_t staDns2[4] = {};
  // SELECT_PREFERRED_SCALE payload (also safe unused for other commands).
  char scaleSelectMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char scaleSelectName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  bool succeeded = false;
  CommandResultState resultState = CommandResultState::NONE;
};

static_assert(sizeof(WebCommand) <= 512, "WebCommand too large for queue");


inline const char *commandResultStateName(CommandResultState state) {
  switch (state) {
    case CommandResultState::NONE: return "NONE";
    case CommandResultState::QUEUED: return "QUEUED";
    case CommandResultState::RESERVED: return "RESERVED";
    case CommandResultState::APPLIED: return "APPLIED";
    case CommandResultState::PERSISTED: return "PERSISTED";
    case CommandResultState::FAILED: return "FAILED";
    case CommandResultState::CANCELED: return "CANCELED";
  }
  return "UNKNOWN";
}

struct LastCycleSummary {
  bool valid = false;
  uint32_t cycleId = 0;
  uint32_t durationMs = 0;
  uint32_t endedAtMs = 0;
  EndReason endReason = EndReason::NONE;
  ControlSource source = ControlSource::NONE;
  bool weightValid = false;
  float lastWeightG = 0.0f;
  uint32_t weightAgeAtEndMs = 0;
  WeightControlState weightControlState = WeightControlState::INACTIVE;
  bool calibrationEligible = false;
};

enum class LastShotType : uint8_t {
  AUTO = 0,
  TIMER_ONLY = 1,
  MANUAL = 2,
  RINSE = 3
};

inline const char *lastShotTypeName(LastShotType type) {
  switch (type) {
    case LastShotType::AUTO: return "auto";
    case LastShotType::TIMER_ONLY: return "timer_only";
    case LastShotType::MANUAL: return "manual";
    case LastShotType::RINSE: return "rinse";
  }
  return "unknown";
}

inline LastShotType lastShotTypeFromCycle(StopperState state,
                                          bool startedWithScale, bool timerOnly,
                                          bool automaticBrew) {
  if (state == StopperState::RINSE) {
    return LastShotType::RINSE;
  }
  if (state == StopperState::MANUAL_NO_SCALE || !startedWithScale) {
    return LastShotType::MANUAL;
  }
  if (timerOnly) {
    return LastShotType::TIMER_ONLY;
  }
  if (automaticBrew) {
    return LastShotType::AUTO;
  }
  return LastShotType::MANUAL;
}

struct PersistedLastShot {
  bool valid = false;
  uint32_t cycleId = 0;
  uint32_t durationMs = 0;
  EndReason endReason = EndReason::NONE;
  float currentWeightG = 0.0f;
  bool weightValid = false;
  uint8_t goalWeightG = 0;
  bool extractionExtended = false;
  float activeStopWeightG = 0.0f;
  uint32_t firstDropElapsedMs = 0;
  bool retarePerformed = false;
  uint8_t shotType = 0;
  bool scaleAvailable = false;
  bool fastExtractionGuardEnabled = false;
  bool slowExtractionGuardEnabled = false;
  bool slowExtractionExtended = false;
  bool autoToManualGuardEnabled = false;
  bool autoToManualGuardEnforced = false;
  bool autoToManualGuardArmed = false;
  uint32_t autoToManualGuardRemainingMs = 0;
  uint32_t minBrewTimeRemainingMs = 0;
  bool noScaleShotGuardEnabled = false;
  bool noScaleShotGuardArmed = false;
  char scaleProtocol[20] = "none";
};

struct ControlStatusSnapshot {
  StopperState state = StopperState::REQUIRES_OFF;
  bool activeCycle = false;
  bool relayClosed = false;
  bool physicalPaddleOn = false;
  bool virtualPaddleOn = false;
  bool remoteControlEnabled = REMOTE_CN9_CONTROL_ENABLED;
  ControlSource source = ControlSource::NONE;
  uint32_t cycleId = 0;
  uint32_t bootId = 0;
  bool maintenanceLeaseActive = false;
  uint32_t maintenanceLeaseId = 0;
  uint32_t maintenanceStartedAtMs = 0;
  uint32_t cn9ElapsedMs = 0;
  RelaySafetyState safetyState = RelaySafetyState::BOOT_SAFE;
  RelaySafetyFault safetyFault = RelaySafetyFault::NONE;
  uint32_t safetyGeneration = 0;
  bool safetyTimersReady = false;
  bool taskWatchdogReady = false;
  bool externalSafetyPresent = false;
  bool cn9FeedbackClosed = false;
  uint32_t resetReasonCode = 0;
  uint32_t unsafeResetCount = 0;
  bool resetRecoveryRequired = false;
  bool bootLoopDetected = false;
  bool scaleAvailable = false;
  WeightStreamState weightStreamState = WeightStreamState::NO_SAMPLE;
  WeightControlState weightControlState = WeightControlState::INACTIVE;
  bool currentWeightValid = false;
  float currentWeightG = 0.0f;
  uint32_t currentWeightAgeMs = 0;
  bool observedWeightValid = false;
  float observedWeightG = 0.0f;
  uint32_t observedWeightAgeMs = 0;
  bool currentTimerValid = false;
  uint32_t currentTimerMs = 0;
  uint32_t currentTimerAgeMs = 0;
  uint32_t scaleConnectionGeneration = 0;
  uint32_t scalePacketSequence = 0;
  uint32_t scalePacketGaps = 0;
  uint32_t scaleRejectedPackets = 0;
  uint32_t scaleReconnects = 0;
  uint8_t scaleLastDisconnectReason = 0;
  uint32_t uptimeMs = 0;
  uint32_t loopMaxGapMs = 0;
  uint32_t loopStackMinWords = 0;
  uint32_t scaleStackMinWords = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minimumFreeHeapBytes = 0;
  uint32_t largestFreeHeapBlockBytes = 0;
  uint32_t psramSizeBytes = 0;
  uint32_t psramFreeBytes = 0;
  uint32_t psramLargestFreeBlockBytes = 0;
  uint32_t scaleEventsDropped = 0;
  RuntimeConfig config = {};
  ShotPresetBank presets = {};
  LastCycleSummary lastCycle = {};
  PersistedLastShot lastShot = {};
  HwmonSnapshot hwmon = {};
  uint32_t debugEventsDropped = 0;
  bool cycleFlowDuringRetare = false;
  bool cycleRetarePerformed = false;
  bool cycleStartedWithScale = false;
  bool cycleAutomaticBrew = false;
  bool cycleTimerOnly = false;
  uint32_t cycleFirstDropMs = 0;
  uint32_t cycleRetareFlowFirstDetectedAtMs = 0;
  uint32_t cycleStartedAtMs = 0;
  uint32_t cycleElapsedMs = 0;
  bool cycleExtractionExtended = false;
  bool cycleSlowExtractionExtended = false;
  bool cycleTargetReachedEarly = false;
  float cycleActiveStopWeightG = 0.0f;
  uint32_t cycleMinBrewTimeRemainingMs = 0;
  bool cycleAutoToManualGuardArmed = false;
  bool cycleAutoToManualGuardEnforced = false;
  uint32_t cycleAutoToManualGuardRemainingMs = 0;
  uint32_t autoToManualGuardTrendMs = DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  char scaleProtocol[20] = "none";
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY] = {};
  uint32_t scaleMacCachePauseRemainingMs = 0;
  bool noScaleShotGuardEnabled = true;
  bool noScaleShotGuardArmed = true;
  bool configPersistPending = false;
  bool configPersistFailed = false;
  bool bleCompanionEnabled = true;
  bool bleCompanionActive = false;
  bool bleCompanionRestartRequired = false;
  bool bleCompanionStackReady = false;
  bool bleCompanionAdvertising = false;
  bool bleCompanionConnected = false;
  uint8_t bleCompanionProtocolVersion = 2;
  uint32_t bleCompanionAcceptedWrites = 0;
  uint32_t bleCompanionRejectedWrites = 0;
  uint8_t bleCompanionLastReject = 0;
};

inline bool controlAllowsConfiguration(const ControlStatusSnapshot &status) {
  // Relay safety is enforced independently when CN9 is armed. A safety
  // lockout must keep the relay open, but it must not lock the WebUI or
  // prevent recovery-time configuration and diagnostics.
  return status.state == StopperState::READY && !status.activeCycle &&
         !status.relayClosed && !status.physicalPaddleOn &&
         !status.maintenanceLeaseActive;
}

enum class DebugCategory : uint8_t {
  PADDLE,
  RELAY,
  STATE,
  SCALE,
  CONFIG,
  NETWORK,
  SECURITY,
  WEB,
  BOOT,
  SYSTEM
};

enum class Cn9ArmFailReason : int32_t {
  INVALID_LIMIT = 1,
  SAFETY_LOCKOUT = 2,
  SUPERVISOR_UNAVAILABLE = 3,
  FEEDBACK_STUCK_CLOSED = 4,
  TIMER_ARM_FAILED = 5,
  ARM_CANCELED = 6
};

enum class DebugCode : uint8_t {
  PADDLE_ON,
  PADDLE_OFF,
  RELAY_CLOSED,
  RELAY_OPENED,
  HARD_LIMIT,
  OPERATIONAL_LIMIT,
  STATE_TRANSITION,
  SCALE_CONNECTING,
  SCALE_CONNECTED,
  SCALE_DISCONNECTED,
  SCALE_TIMER_START_OK,
  SCALE_TIMER_START_FAILED,
  SCALE_TIMER_STOP_OK,
  SCALE_TIMER_STOP_FAILED,
  SCALE_BEEP_OK,
  SCALE_BEEP_FAILED,
  SCALE_BEEP_UNSUPPORTED,
  SCALE_PADDLE_REMINDER_BEEP_OK,
  SCALE_PADDLE_REMINDER_BEEP_FAILED,
  SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED,
  SCALE_DEBUG_OK,
  SCALE_DEBUG_FAILED,
  SCALE_DEBUG_UNSUPPORTED,
  CONFIG_ACCEPTED,
  CONFIG_REJECTED,
  WEIGHT_OFFSET_RESET,
  AUTO_TO_MANUAL_GUARD_SAMPLES_RESET,
  AUTO_TO_MANUAL_GUARD_ARMED,
  AUTO_TO_MANUAL_GUARD_ENFORCED,
  AUTO_TO_MANUAL_GUARD_CLEARED,
  AUTO_TO_MANUAL_GUARD_FIRED,
  CONFIG_PERSISTED,
  AP_STARTED,
  AP_STOPPED,
  STA_CONNECTING,
  STA_CONNECTED,
  STA_FAILED,
  WIFI_SCAN_STARTED,
  WIFI_SCAN_COMPLETE,
  WIFI_SCAN_ERROR,
  WIFI_SCAN_CANCELED,
  WEB_PADDLE_ON,
  WEB_PADDLE_OFF,
  WEB_RINSE,
  WEB_COMMAND_ACCEPTED,
  WEB_COMMAND_REJECTED,
  WEB_STOP,
  RESTART_REQUESTED,
  NETWORK_RESET,
  FACTORY_RESET,
  MAINTENANCE_RESERVED,
  MAINTENANCE_COMPLETED,
  MAINTENANCE_CANCELED,
  COMMAND_RETRY,
  COMMAND_FAILED,
  SCALE_EVENT_DROPPED,
  SCALE_SAMPLE_REJECTED_INVALID,
  SCALE_SAMPLE_REJECTED_RANGE,
  SCALE_SAMPLE_REJECTED_SLEW,
  SCALE_SAMPLE_REJECTED_RECOVERY,
  SCALE_SAMPLE_REJECTED_PRE_CYCLE,
  SCALE_POST_TARE_BASELINE_TIMEOUT,
  SCALE_STREAM_STALE,
  SCALE_CONTROL_SUSPENDED,
  SCALE_CONTROL_RECOVERED,
  SCALE_THRESHOLD_CONFIRMED,
  SCALE_OVERLOAD_CONFIRMED,
  SCALE_STALE_EVENT_REJECTED,
  SCALE_PACKET_GAP,
  NETWORK_RETRY,
  INITIALIZATION_FAILED,
  TIME_SYNC_OK,
  TIME_SYNC_FAIL,
  FIRST_DROP_DURING_RETARE,
  FAST_EXTRACTION_ENTERED,
  FAST_EXTRACTION_STOP_MAX,
  FAST_EXTRACTION_STOP_MIN_TIME,
  SLOW_EXTRACTION_ENTERED,
  SLOW_EXTRACTION_STOP_MAX_TIME,
  SLOW_EXTRACTION_STOP_MIN_WEIGHT,
  SHOT_LOG_PERSIST_FAILED,
  RUNTIME_PERSIST_FAILED,
  BOOT_BANNER,
  BOOT_RESET_REASON,
  BOOT_SUBSYSTEM,
  BOOT_RUNTIME_CONFIG,
  BOOT_READY,
  SAFETY_LOCKOUT_ACTIVE,
  CN9_ARM_FAILED,
  CYCLE_STARTED,
  CYCLE_ENDED,
  BREW_STARTED,
  TIMER_ONLY_BREW_STARTED,
  MANUAL_CYCLE_STARTED,
  NO_SCALE_SHOT_GUARD_BLOCKED,
  NO_SCALE_SHOT_GUARD_ARMED,
  NO_SCALE_SHOT_GUARD_CONSUMED,
  CUP_START_GUARD_BLOCKED,
  CUP_REMOVED_CONFIRMED,
  RINSE_CLASSIFIED,
  SYSTEM_LOG_OVERRUN,
  AP_PASSWORD_RESET,
  HEALTH_HEAP_LOW,
  HEALTH_STACK_LOW,
  HEALTH_LOOP_GAP
};

// Health telemetry thresholds (observability only; never act on CN9).
// Clear values are higher than alert values for hysteresis / rising-edge only.
constexpr uint32_t HEALTH_HEAP_FREE_ALERT_BYTES = 49152;   // 48 KiB
constexpr uint32_t HEALTH_HEAP_FREE_CLEAR_BYTES = 65536;   // 64 KiB
constexpr uint32_t HEALTH_HEAP_LARGEST_ALERT_BYTES = 16384; // 16 KiB
constexpr uint32_t HEALTH_HEAP_LARGEST_CLEAR_BYTES = 24576; // 24 KiB
constexpr uint32_t HEALTH_STACK_MIN_ALERT_WORDS = 256;      // 1 KiB remaining
constexpr uint32_t HEALTH_STACK_MIN_CLEAR_WORDS = 384;
constexpr uint32_t HEALTH_LOOP_GAP_ALERT_MS = 200;
constexpr uint32_t HEALTH_LOOP_GAP_CLEAR_MS = 80;

// Bitmask for argument2 on RUNTIME_PERSIST_FAILED (what was pending in NVS).
constexpr int32_t RUNTIME_PERSIST_REASON_OFFSET = 1;
constexpr int32_t RUNTIME_PERSIST_REASON_ATM_SAMPLES = 2;
constexpr int32_t RUNTIME_PERSIST_REASON_USER = 4;
constexpr int32_t RUNTIME_PERSIST_REASON_SCALE_MAC = 8;

// argument1 values for BOOT_SUBSYSTEM / INITIALIZATION_FAILED.
constexpr int32_t BOOT_SUBSYSTEM_CPU = 1;
constexpr int32_t BOOT_SUBSYSTEM_PERSISTENCE = 2;
constexpr int32_t BOOT_SUBSYSTEM_BLE = 3;
constexpr int32_t BOOT_SUBSYSTEM_SETTINGS_SAVE = 4;
constexpr int32_t BOOT_SUBSYSTEM_RELAY_TIMERS = 5;
constexpr int32_t BOOT_SUBSYSTEM_TASK_WDT = 6;
constexpr int32_t BOOT_SUBSYSTEM_SCALE_WORKER = 7;
constexpr int32_t BOOT_SUBSYSTEM_WEB_QUEUE = 8;
constexpr int32_t BOOT_SUBSYSTEM_NETWORK = 9;
constexpr int32_t BOOT_SUBSYSTEM_PSRAM = 11;

struct DebugEvent {
  uint32_t sequence = 0;
  uint32_t atMs = 0;
  uint32_t wallSec = 0;
  LogLevel level = LogLevel::INFO;
  DebugCategory category = DebugCategory::STATE;
  DebugCode code = DebugCode::STATE_TRANSITION;
  int32_t argument1 = 0;
  int32_t argument2 = 0;
};

class DebugRingBuffer {
 public:
  void clear() {
    nextSequence_ = 1;
    count_ = 0;
    writeIndex_ = 0;
    overwritten_ = 0;
    for (DebugEvent &event : events_) {
      event = DebugEvent{};
    }
  }

  void add(uint32_t atMs, uint32_t wallSec, LogLevel level,
           DebugCategory category, DebugCode code, int32_t argument1 = 0,
           int32_t argument2 = 0) {
    if (count_ == DEBUG_EVENT_CAPACITY) {
      ++overwritten_;
    } else {
      ++count_;
    }
    DebugEvent &event = events_[writeIndex_];
    event.sequence = nextSequence_++;
    if (nextSequence_ == 0) {
      nextSequence_ = 1;
    }
    event.atMs = atMs;
    event.wallSec = wallSec;
    event.level = level;
    event.category = category;
    event.code = code;
    event.argument1 = argument1;
    event.argument2 = argument2;
    writeIndex_ = (writeIndex_ + 1) % DEBUG_EVENT_CAPACITY;
  }

  size_t copyAfter(uint32_t afterSequence, DebugEvent *output,
                   size_t outputCapacity) const {
    if (output == nullptr || outputCapacity == 0 || count_ == 0) {
      return 0;
    }
    const size_t oldest =
        (writeIndex_ + DEBUG_EVENT_CAPACITY - count_) % DEBUG_EVENT_CAPACITY;
    size_t copied = 0;
    for (size_t index = 0; index < count_ && copied < outputCapacity; ++index) {
      const DebugEvent &event =
          events_[(oldest + index) % DEBUG_EVENT_CAPACITY];
      if (static_cast<int32_t>(event.sequence - afterSequence) > 0) {
        output[copied++] = event;
      }
    }
    return copied;
  }

  size_t countAfter(uint32_t afterSequence) const {
    if (count_ == 0) {
      return 0;
    }
    const size_t oldest =
        (writeIndex_ + DEBUG_EVENT_CAPACITY - count_) % DEBUG_EVENT_CAPACITY;
    size_t matching = 0;
    for (size_t index = 0; index < count_; ++index) {
      const DebugEvent &event =
          events_[(oldest + index) % DEBUG_EVENT_CAPACITY];
      if (static_cast<int32_t>(event.sequence - afterSequence) > 0) {
        ++matching;
      }
    }
    return matching;
  }

  bool copyFirstAfter(uint32_t afterSequence, DebugEvent &output) const {
    if (count_ == 0) {
      return false;
    }
    const size_t oldest =
        (writeIndex_ + DEBUG_EVENT_CAPACITY - count_) % DEBUG_EVENT_CAPACITY;
    for (size_t index = 0; index < count_; ++index) {
      const DebugEvent &event =
          events_[(oldest + index) % DEBUG_EVENT_CAPACITY];
      if (static_cast<int32_t>(event.sequence - afterSequence) > 0) {
        output = event;
        return true;
      }
    }
    return false;
  }

  uint32_t overwritten() const { return overwritten_; }

 private:
  DebugEvent events_[DEBUG_EVENT_CAPACITY] = {};
  uint32_t nextSequence_ = 1;
  size_t count_ = 0;
  size_t writeIndex_ = 0;
  uint32_t overwritten_ = 0;
};

inline const char *logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::CRITICAL: return "critical";
    case LogLevel::ERROR: return "error";
    case LogLevel::WARNING: return "warning";
    case LogLevel::INFO: return "info";
    case LogLevel::DEBUG: return "debug";
    case LogLevel::NONE: return "none";
  }
  return "unknown";
}

inline bool parseLogLevel(const char *text, uint8_t &level) {
  if (text == nullptr) {
    return false;
  }
  if (strcmp(text, "critical") == 0) {
    level = static_cast<uint8_t>(LogLevel::CRITICAL);
    return true;
  }
  if (strcmp(text, "error") == 0) {
    level = static_cast<uint8_t>(LogLevel::ERROR);
    return true;
  }
  if (strcmp(text, "warning") == 0) {
    level = static_cast<uint8_t>(LogLevel::WARNING);
    return true;
  }
  if (strcmp(text, "info") == 0) {
    level = static_cast<uint8_t>(LogLevel::INFO);
    return true;
  }
  if (strcmp(text, "debug") == 0) {
    level = static_cast<uint8_t>(LogLevel::DEBUG);
    return true;
  }
  if (strcmp(text, "none") == 0) {
    level = static_cast<uint8_t>(LogLevel::NONE);
    return true;
  }
  return false;
}

inline char logLevelLetter(LogLevel level) {
  switch (level) {
    case LogLevel::CRITICAL: return 'C';
    case LogLevel::ERROR: return 'E';
    case LogLevel::WARNING: return 'W';
    case LogLevel::INFO: return 'I';
    case LogLevel::DEBUG: return 'D';
    case LogLevel::NONE: return '-';
  }
  return '?';
}

inline bool logLevelAtMost(LogLevel level, LogLevel threshold) {
  // NONE is the serial-off sentinel, not "more verbose than DEBUG".
  if (threshold == LogLevel::NONE) {
    return false;
  }
  return static_cast<uint8_t>(level) <= static_cast<uint8_t>(threshold);
}

inline LogLevel debugCodeDefaultLevel(DebugCode code) {
  switch (code) {
    case DebugCode::INITIALIZATION_FAILED:
    case DebugCode::CN9_ARM_FAILED:
    case DebugCode::SAFETY_LOCKOUT_ACTIVE:
    case DebugCode::HARD_LIMIT:
      return LogLevel::CRITICAL;
    case DebugCode::SCALE_TIMER_START_FAILED:
    case DebugCode::SCALE_TIMER_STOP_FAILED:
    case DebugCode::SCALE_BEEP_FAILED:
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_FAILED:
    case DebugCode::SCALE_DEBUG_FAILED:
    case DebugCode::STA_FAILED:
    case DebugCode::COMMAND_FAILED:
    case DebugCode::SHOT_LOG_PERSIST_FAILED:
    case DebugCode::RUNTIME_PERSIST_FAILED:
    case DebugCode::SCALE_STREAM_STALE:
    case DebugCode::SCALE_CONTROL_SUSPENDED:
    case DebugCode::SCALE_BEEP_UNSUPPORTED:
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED:
    case DebugCode::SCALE_DEBUG_UNSUPPORTED:
      return LogLevel::ERROR;
    case DebugCode::SCALE_SAMPLE_REJECTED_INVALID:
    case DebugCode::SCALE_SAMPLE_REJECTED_RANGE:
    case DebugCode::SCALE_SAMPLE_REJECTED_SLEW:
    case DebugCode::SCALE_SAMPLE_REJECTED_RECOVERY:
    case DebugCode::SCALE_SAMPLE_REJECTED_PRE_CYCLE:
    case DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT:
    case DebugCode::SCALE_EVENT_DROPPED:
    case DebugCode::SCALE_PACKET_GAP:
    case DebugCode::SCALE_STALE_EVENT_REJECTED:
    case DebugCode::CONFIG_REJECTED:
    case DebugCode::NETWORK_RETRY:
    case DebugCode::WIFI_SCAN_ERROR:
    case DebugCode::WIFI_SCAN_CANCELED:
    case DebugCode::TIME_SYNC_FAIL:
    case DebugCode::WEB_COMMAND_REJECTED:
    case DebugCode::COMMAND_RETRY:
    case DebugCode::OPERATIONAL_LIMIT:
    case DebugCode::SYSTEM_LOG_OVERRUN:
    case DebugCode::FIRST_DROP_DURING_RETARE:
    case DebugCode::HEALTH_HEAP_LOW:
    case DebugCode::HEALTH_STACK_LOW:
    case DebugCode::HEALTH_LOOP_GAP:
      return LogLevel::WARNING;
    case DebugCode::SCALE_CONNECTING:
    case DebugCode::SCALE_TIMER_START_OK:
    case DebugCode::SCALE_TIMER_STOP_OK:
    case DebugCode::SCALE_BEEP_OK:
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_OK:
    case DebugCode::SCALE_DEBUG_OK:
    case DebugCode::STA_CONNECTING:
    case DebugCode::WIFI_SCAN_STARTED:
    case DebugCode::WIFI_SCAN_COMPLETE:
    case DebugCode::WEB_COMMAND_ACCEPTED:
    case DebugCode::MAINTENANCE_RESERVED:
    case DebugCode::MAINTENANCE_COMPLETED:
    case DebugCode::MAINTENANCE_CANCELED:
      return LogLevel::DEBUG;
    default:
      return LogLevel::INFO;
  }
}

inline const char *debugCategoryName(DebugCategory category) {
  switch (category) {
    case DebugCategory::PADDLE: return "paddle";
    case DebugCategory::RELAY: return "relay";
    case DebugCategory::STATE: return "state";
    case DebugCategory::SCALE: return "scale";
    case DebugCategory::CONFIG: return "config";
    case DebugCategory::NETWORK: return "network";
    case DebugCategory::SECURITY: return "security";
    case DebugCategory::WEB: return "web";
    case DebugCategory::BOOT: return "boot";
    case DebugCategory::SYSTEM: return "system";
  }
  return "unknown";
}

inline const char *cn9ArmFailReasonName(Cn9ArmFailReason reason) {
  switch (reason) {
    case Cn9ArmFailReason::INVALID_LIMIT: return "invalid safety limit";
    case Cn9ArmFailReason::SAFETY_LOCKOUT: return "safety lockout is active";
    case Cn9ArmFailReason::SUPERVISOR_UNAVAILABLE:
      return "safety supervisor unavailable";
    case Cn9ArmFailReason::FEEDBACK_STUCK_CLOSED:
      return "feedback is already closed";
    case Cn9ArmFailReason::TIMER_ARM_FAILED:
      return "failed to arm safety deadline";
    case Cn9ArmFailReason::ARM_CANCELED:
      return "arm transaction was canceled";
  }
  return "unknown";
}

inline const char *bootSubsystemName(int32_t subsystem) {
  switch (subsystem) {
    case BOOT_SUBSYSTEM_CPU: return "cpu";
    case BOOT_SUBSYSTEM_PERSISTENCE: return "persistence";
    case BOOT_SUBSYSTEM_BLE: return "ble";
    case BOOT_SUBSYSTEM_SETTINGS_SAVE: return "settings_save";
    case BOOT_SUBSYSTEM_RELAY_TIMERS: return "relay_timers";
    case BOOT_SUBSYSTEM_TASK_WDT: return "task_watchdog";
    case BOOT_SUBSYSTEM_SCALE_WORKER: return "scale_worker";
    case BOOT_SUBSYSTEM_WEB_QUEUE: return "web_queue";
    case BOOT_SUBSYSTEM_NETWORK: return "network";
    case BOOT_SUBSYSTEM_PSRAM: return "psram";
  }
  return "unknown";
}

inline const char *debugCodeName(DebugCode code) {
  switch (code) {
    case DebugCode::PADDLE_ON: return "paddle on";
    case DebugCode::PADDLE_OFF: return "paddle off";
    case DebugCode::RELAY_CLOSED: return "CN9 closed";
    case DebugCode::RELAY_OPENED: return "CN9 opened";
    case DebugCode::HARD_LIMIT: return "hard limit reached";
    case DebugCode::OPERATIONAL_LIMIT:
      return "configured wall limit reached";
    case DebugCode::STATE_TRANSITION: return "state transition";
    case DebugCode::SCALE_CONNECTING: return "scale connecting";
    case DebugCode::SCALE_CONNECTED: return "scale connected";
    case DebugCode::SCALE_DISCONNECTED: return "scale disconnected";
    case DebugCode::SCALE_TIMER_START_OK: return "scale timer started";
    case DebugCode::SCALE_TIMER_START_FAILED:
      return "scale timer start failed";
    case DebugCode::SCALE_TIMER_STOP_OK: return "scale timer stopped";
    case DebugCode::SCALE_TIMER_STOP_FAILED:
      return "scale timer stop failed";
    case DebugCode::SCALE_BEEP_OK: return "scale first-drop beep sent";
    case DebugCode::SCALE_BEEP_FAILED:
      return "scale first-drop beep failed";
    case DebugCode::SCALE_BEEP_UNSUPPORTED:
      return "scale has no state-safe beep command";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_OK:
      return "scale paddle-return reminder beep sent";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_FAILED:
      return "scale paddle-return reminder beep failed";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED:
      return "scale has no state-safe paddle-return reminder beep command";
    case DebugCode::SCALE_DEBUG_OK: return "scale debug command sent";
    case DebugCode::SCALE_DEBUG_FAILED: return "scale debug command failed";
    case DebugCode::SCALE_DEBUG_UNSUPPORTED:
      return "scale debug command unsupported";
    case DebugCode::CONFIG_ACCEPTED: return "configuration accepted";
    case DebugCode::CONFIG_REJECTED: return "configuration rejected";
    case DebugCode::WEIGHT_OFFSET_RESET:
      return "learned weight offset reset to default";
    case DebugCode::AUTO_TO_MANUAL_GUARD_SAMPLES_RESET:
      return "auto-to-manual guard samples reset";
    case DebugCode::AUTO_TO_MANUAL_GUARD_ARMED:
      return "auto-to-manual guard armed";
    case DebugCode::AUTO_TO_MANUAL_GUARD_ENFORCED:
      return "auto-to-manual guard enforced";
    case DebugCode::AUTO_TO_MANUAL_GUARD_CLEARED:
      return "auto-to-manual guard enforcement cleared";
    case DebugCode::AUTO_TO_MANUAL_GUARD_FIRED:
      return "auto-to-manual guard opened CN9";
    case DebugCode::CONFIG_PERSISTED: return "configuration persisted";
    case DebugCode::AP_STARTED: return "access point started";
    case DebugCode::AP_STOPPED: return "access point stopped";
    case DebugCode::STA_CONNECTING: return "station connecting";
    case DebugCode::STA_CONNECTED: return "station connected";
    case DebugCode::STA_FAILED: return "station connection failed";
    case DebugCode::WIFI_SCAN_STARTED: return "WiFi scan started";
    case DebugCode::WIFI_SCAN_COMPLETE: return "WiFi scan completed";
    case DebugCode::WIFI_SCAN_ERROR: return "WiFi scan failed";
    case DebugCode::WIFI_SCAN_CANCELED:
      return "WiFi scan canceled for active control";
    case DebugCode::WEB_PADDLE_ON: return "paddle web on";
    case DebugCode::WEB_PADDLE_OFF: return "paddle web off";
    case DebugCode::WEB_RINSE: return "rinse web started";
    case DebugCode::WEB_COMMAND_ACCEPTED: return "web command accepted";
    case DebugCode::WEB_COMMAND_REJECTED: return "web command rejected";
    case DebugCode::WEB_STOP: return "web safe stop";
    case DebugCode::RESTART_REQUESTED: return "restart requested";
    case DebugCode::NETWORK_RESET: return "network settings reset";
    case DebugCode::AP_PASSWORD_RESET: return "AP password restored";
    case DebugCode::FACTORY_RESET: return "factory settings restored";
    case DebugCode::MAINTENANCE_RESERVED:
      return "maintenance lease reserved";
    case DebugCode::MAINTENANCE_COMPLETED:
      return "maintenance lease completed";
    case DebugCode::MAINTENANCE_CANCELED:
      return "maintenance lease canceled";
    case DebugCode::COMMAND_RETRY: return "durable command retry";
    case DebugCode::COMMAND_FAILED: return "durable command failed";
    case DebugCode::SCALE_EVENT_DROPPED: return "scale event dropped";
    case DebugCode::SCALE_SAMPLE_REJECTED_INVALID:
      return "scale sample rejected: invalid weight";
    case DebugCode::SCALE_SAMPLE_REJECTED_RANGE:
      return "scale sample rejected: out of automation range";
    case DebugCode::SCALE_SAMPLE_REJECTED_SLEW:
      return "scale sample rejected: implausible slew";
    case DebugCode::SCALE_SAMPLE_REJECTED_RECOVERY:
      return "scale sample rejected: recovery failed";
    case DebugCode::SCALE_SAMPLE_REJECTED_PRE_CYCLE:
      return "scale sample rejected: pre-cycle event";
    case DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT:
      return "scale post-tare baseline timeout";
    case DebugCode::SCALE_STREAM_STALE: return "scale weight stream stale";
    case DebugCode::SCALE_CONTROL_SUSPENDED:
      return "weight control suspended";
    case DebugCode::SCALE_CONTROL_RECOVERED:
      return "weight control recovered";
    case DebugCode::SCALE_THRESHOLD_CONFIRMED:
      return "scale stop threshold confirmed";
    case DebugCode::SCALE_OVERLOAD_CONFIRMED:
      return "scale overload confirmed";
    case DebugCode::SCALE_STALE_EVENT_REJECTED:
      return "stale scale event rejected";
    case DebugCode::SCALE_PACKET_GAP: return "scale packet gap";
    case DebugCode::NETWORK_RETRY: return "network startup retry";
    case DebugCode::INITIALIZATION_FAILED:
      return "subsystem initialization failed";
    case DebugCode::TIME_SYNC_OK: return "clock synchronized";
    case DebugCode::TIME_SYNC_FAIL: return "clock sync failed";
    case DebugCode::FIRST_DROP_DURING_RETARE:
      return "first coffee drop detected during retare";
    case DebugCode::FAST_EXTRACTION_ENTERED:
      return "fast extraction guard extended shot";
    case DebugCode::FAST_EXTRACTION_STOP_MAX:
      return "fast extraction guard stopped at max weight";
    case DebugCode::FAST_EXTRACTION_STOP_MIN_TIME:
      return "fast extraction guard stopped at min brew time";
    case DebugCode::SLOW_EXTRACTION_ENTERED:
      return "slow extraction guard extended shot";
    case DebugCode::SLOW_EXTRACTION_STOP_MAX_TIME:
      return "slow extraction guard stopped at max brew time";
    case DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT:
      return "slow extraction guard stopped at min weight";
    case DebugCode::SHOT_LOG_PERSIST_FAILED:
      return "shot history NVS persist failed";
    case DebugCode::RUNTIME_PERSIST_FAILED:
      return "workflow NVS persist failed";
    case DebugCode::BOOT_BANNER: return "firmware boot";
    case DebugCode::BOOT_RESET_REASON: return "reset reason";
    case DebugCode::BOOT_SUBSYSTEM: return "boot subsystem";
    case DebugCode::BOOT_RUNTIME_CONFIG: return "runtime config loaded";
    case DebugCode::BOOT_READY: return "boot ready";
    case DebugCode::SAFETY_LOCKOUT_ACTIVE: return "safety lockout active";
    case DebugCode::CN9_ARM_FAILED: return "cannot close CN9";
    case DebugCode::CYCLE_STARTED: return "cycle started";
    case DebugCode::CYCLE_ENDED: return "cycle ended";
    case DebugCode::BREW_STARTED: return "brew started";
    case DebugCode::TIMER_ONLY_BREW_STARTED: return "timer-only brew started";
    case DebugCode::MANUAL_CYCLE_STARTED: return "manual cycle started";
    case DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED:
      return "no-scale BBW shot blocked";
    case DebugCode::NO_SCALE_SHOT_GUARD_ARMED:
      return "no-scale BBW guard armed";
    case DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED:
      return "no-scale BBW guard consumed";
    case DebugCode::CUP_START_GUARD_BLOCKED:
      return "brew blocked: cup required";
    case DebugCode::CUP_REMOVED_CONFIRMED:
      return "shot stopped: cup removed";
    case DebugCode::RINSE_CLASSIFIED: return "rinse classified";
    case DebugCode::SYSTEM_LOG_OVERRUN: return "diagnostic log overrun";
    case DebugCode::HEALTH_HEAP_LOW: return "health heap low";
    case DebugCode::HEALTH_STACK_LOW: return "health stack low";
    case DebugCode::HEALTH_LOOP_GAP: return "health loop gap high";
  }
  return "unknown";
}

inline int32_t weightToCentigrams(float weightG) {
  if (!isfinite(weightG)) {
    return 0;
  }
  return static_cast<int32_t>(weightG * 100.0f);
}

inline void formatWeightCentigrams(int32_t centigrams, char *buffer,
                                   size_t capacity) {
  if (buffer == nullptr || capacity == 0) {
    return;
  }
  const int32_t whole = centigrams / 100;
  const int32_t fraction =
      centigrams >= 0 ? centigrams % 100 : -((-centigrams) % 100);
  snprintf(buffer, capacity, "%ld.%02ldg", static_cast<long>(whole),
           static_cast<long>(fraction));
}

inline bool formatScaleSampleDebugMessage(const DebugEvent &event, char *message,
                                          size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return false;
  }
  char weightText[24] = {};
  char referenceText[24] = {};
  switch (event.code) {
    case DebugCode::SCALE_SAMPLE_REJECTED_INVALID:
    case DebugCode::SCALE_SAMPLE_REJECTED_PRE_CYCLE:
      formatWeightCentigrams(event.argument1, weightText, sizeof(weightText));
      snprintf(message, capacity, "%s (%s)", debugCodeName(event.code),
               weightText);
      return true;
    case DebugCode::SCALE_SAMPLE_REJECTED_RANGE:
      formatWeightCentigrams(event.argument1, weightText, sizeof(weightText));
      formatWeightCentigrams(event.argument2, referenceText,
                             sizeof(referenceText));
      snprintf(message, capacity, "%s (%s, limit=%s)",
               debugCodeName(event.code), weightText, referenceText);
      return true;
    case DebugCode::SCALE_SAMPLE_REJECTED_SLEW:
    case DebugCode::SCALE_SAMPLE_REJECTED_RECOVERY:
      formatWeightCentigrams(event.argument1, weightText, sizeof(weightText));
      formatWeightCentigrams(event.argument2, referenceText,
                             sizeof(referenceText));
      snprintf(message, capacity, "%s (weight=%s, reference=%s)",
               debugCodeName(event.code), weightText, referenceText);
      return true;
    case DebugCode::SCALE_POST_TARE_BASELINE_TIMEOUT:
      snprintf(message, capacity, "%s (cycle=%ld, graceMs=%ld)",
               debugCodeName(event.code),
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    default:
      return false;
  }
}

inline const char *runtimePersistReasonLabel(int32_t reasonBits) {
  const bool offset = (reasonBits & RUNTIME_PERSIST_REASON_OFFSET) != 0;
  const bool samples =
      (reasonBits & RUNTIME_PERSIST_REASON_ATM_SAMPLES) != 0;
  if (offset && samples) {
    return "A->M samples+offset";
  }
  if (samples) {
    return "A->M duration samples";
  }
  if (offset) {
    return "learned weight offset";
  }
  return "runtime config";
}

// Builds Web UI /api/v1/log lines for NVS persist failures with origin clues.
inline bool formatPersistDebugMessage(const DebugEvent &event, char *message,
                                      size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return false;
  }
  switch (event.code) {
    case DebugCode::SHOT_LOG_PERSIST_FAILED:
      snprintf(message, capacity,
               "shot history NVS write failed "
               "(ShotLog.save shotlog/records bytes=%ld count=%ld)",
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    case DebugCode::RUNTIME_PERSIST_FAILED:
      snprintf(message, capacity,
               "workflow NVS write failed "
               "(PERSIST_RUNTIME settingsA/B rev=%ld; %s)",
               static_cast<long>(event.argument1),
               runtimePersistReasonLabel(event.argument2));
      return true;
    default:
      return false;
  }
}

inline const char *endReasonDebugName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "none";
    case EndReason::PADDLE: return "paddle";
    case EndReason::SCALE_THRESHOLD: return "scale threshold";
    case EndReason::WEIGHT_ANOMALY: return "weight anomaly";
    case EndReason::GLOBAL_LIMIT: return "global CN9 limit";
    case EndReason::CONFIGURED_WALL_LIMIT: return "configured wall limit";
    case EndReason::SHORT_SHOT: return "short shot";
    case EndReason::RINSE_COMPLETE: return "rinse complete";
    case EndReason::WEB_STOP: return "web stop";
    case EndReason::PHYSICAL_OVERRIDE: return "physical override";
    case EndReason::WEB_HEARTBEAT_TIMEOUT: return "web heartbeat timeout";
    case EndReason::RELAY_SAFETY_FAILURE: return "relay safety failure";
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
      return "fast extraction max weight";
    case EndReason::FAST_EXTRACTION_MIN_TIME:
      return "fast extraction min time";
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
      return "slow extraction max time";
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
      return "slow extraction min weight";
    case EndReason::AUTO_TO_MANUAL_GUARD: return "auto-to-manual time guard";
    case EndReason::CUP_REMOVED: return "cup removed";
  }
  return "unknown";
}

inline bool formatLifecycleDebugMessage(const DebugEvent &event, char *message,
                                        size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return false;
  }
  char weightText[24] = {};
  char offsetText[24] = {};
  switch (event.code) {
    case DebugCode::BOOT_BANNER:
      snprintf(message, capacity, "firmware boot (bootId=%ld)",
               static_cast<long>(event.argument1));
      return true;
    case DebugCode::BOOT_RESET_REASON:
      snprintf(message, capacity, "reset reason code=%ld",
               static_cast<long>(event.argument1));
      return true;
    case DebugCode::BOOT_SUBSYSTEM:
      snprintf(message, capacity, "%s=%s",
               bootSubsystemName(event.argument1),
               event.argument2 != 0 ? "ok" : "fail");
      return true;
    case DebugCode::BOOT_RUNTIME_CONFIG:
      formatWeightCentigrams(event.argument1, weightText, sizeof(weightText));
      formatWeightCentigrams(event.argument2, offsetText, sizeof(offsetText));
      snprintf(message, capacity, "runtime config goal=%s offset=%s",
               weightText, offsetText);
      return true;
    case DebugCode::CN9_ARM_FAILED:
      snprintf(message, capacity, "cannot close CN9: %s",
               cn9ArmFailReasonName(
                   static_cast<Cn9ArmFailReason>(event.argument1)));
      return true;
    case DebugCode::CYCLE_STARTED:
      formatWeightCentigrams(event.argument1, weightText, sizeof(weightText));
      formatWeightCentigrams(event.argument2, offsetText, sizeof(offsetText));
      snprintf(message, capacity, "cycle started; goal=%s offset=%s",
               weightText, offsetText);
      return true;
    case DebugCode::NO_SCALE_SHOT_GUARD_BLOCKED:
      snprintf(message, capacity, "no-scale BBW shot blocked");
      return true;
    case DebugCode::NO_SCALE_SHOT_GUARD_ARMED:
      snprintf(message, capacity, "no-scale BBW guard armed");
      return true;
    case DebugCode::NO_SCALE_SHOT_GUARD_CONSUMED:
      snprintf(message, capacity, "no-scale BBW guard consumed");
      return true;
    case DebugCode::CUP_START_GUARD_BLOCKED:
      snprintf(message, capacity, "brew blocked: cup required");
      return true;
    case DebugCode::CUP_REMOVED_CONFIRMED:
      snprintf(message, capacity, "shot stopped: cup removed");
      return true;
    case DebugCode::CYCLE_ENDED:
      snprintf(message, capacity, "cycle ended by %s",
               endReasonDebugName(static_cast<EndReason>(event.argument1)));
      return true;
    case DebugCode::SYSTEM_LOG_OVERRUN:
      snprintf(message, capacity, "diagnostic log overrun dropped=%ld",
               static_cast<long>(event.argument1));
      return true;
    case DebugCode::HEALTH_HEAP_LOW:
      snprintf(message, capacity,
               "health heap low free=%ld largest=%ld",
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    case DebugCode::HEALTH_STACK_LOW:
      snprintf(message, capacity,
               "health stack low loop=%ld scale=%ld words",
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    case DebugCode::HEALTH_LOOP_GAP:
      snprintf(message, capacity, "health loop gap high max=%ld ms",
               static_cast<long>(event.argument1));
      return true;
    default:
      return false;
  }
}

inline uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return crc;
}

inline uint32_t crc32(const uint8_t *data, size_t length) {
  return ~crc32Update(0xFFFFFFFFU, data, length);
}

}  // namespace shotstopper
