#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ShotStopperSafety.h"

namespace shotstopper {

constexpr uint32_t CONFIG_SCHEMA_VERSION = 12;
constexpr uint32_t PREVIOUS_CONFIG_SCHEMA_VERSION = 11;
constexpr size_t NTP_SERVER_HOST_CAPACITY = 64;
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 3600UL * 1000UL;
constexpr uint32_t NTP_UNSYNCED_RETRY_MS = 60UL * 1000UL;
constexpr uint32_t NTP_FIRST_SYNC_TIMEOUT_MS = 30000;
constexpr uint32_t NTP_STA_SETTLE_MS = 8000;
constexpr uint32_t NTP_SYNC_TIMEOUT_MS = 60000;
constexpr uint32_t NTP_STALE_AFTER_MS = 24UL * 3600UL * 1000UL;
constexpr uint8_t NTP_MAX_CONSECUTIVE_FAILURES = 255;

enum class NtpServerPreset : uint8_t {
  POOL = 0,
  GOOGLE = 1,
  CLOUDFLARE = 2,
  NIST = 3
};

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
constexpr uint32_t LEGACY_PRE_SCHEMA_FOUR_VERSION = 3;
constexpr uint32_t LEGACY_SCHEMA_FOUR_VERSION = 4;
constexpr uint32_t LEGACY_SCHEMA_FIVE_VERSION = 5;
constexpr uint32_t LEGACY_CONFIG_SCHEMA_VERSION = 2;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS = 10000;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_INTERVAL_MS = 5000;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_INTERVAL_MS = 60000;
constexpr uint32_t DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    15UL * 60UL * 1000UL;
constexpr uint32_t MIN_PADDLE_RETURN_REMINDER_MAX_DURATION_MS = 60000UL;
constexpr uint32_t MAX_PADDLE_RETURN_REMINDER_MAX_DURATION_MS =
    60UL * 60UL * 1000UL;
constexpr uint32_t HARD_MAX_CN9_CLOSED_MS = 60000;
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS = 60000;
constexpr uint32_t DEFAULT_RINSE_GESTURE_MS = 1500;
constexpr uint32_t DEFAULT_RINSE_DURATION_MS = 3000;
constexpr uint32_t DEFAULT_RETARE_WINDOW_MS = 4000;
constexpr uint32_t DEFAULT_CONFIRMATION_TIMEOUT_MS = 12000;
constexpr uint32_t MIN_CONFIRMATION_AFTER_RETARE_MS = 3000;
constexpr float DEFAULT_MINIMUM_CUP_WEIGHT_G = 10.0f;
constexpr float MIN_MINIMUM_CUP_WEIGHT_G = 1.0f;
constexpr float MAX_MINIMUM_CUP_WEIGHT_G = 500.0f;
constexpr uint32_t MIN_RETARE_WINDOW_MS = 500;
constexpr uint32_t MAX_RETARE_WINDOW_MS = 10000;
constexpr uint32_t MIN_CONFIRMATION_TIMEOUT_MS = 500;
constexpr uint32_t MAX_CONFIRMATION_TIMEOUT_MS = 30000;
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
constexpr float MAX_OFFSET_G = 5.0f;
constexpr float DEFAULT_WEIGHT_OFFSET_G = 1.5f;
constexpr size_t WIFI_SSID_CAPACITY = 33;
constexpr size_t WIFI_PASSWORD_CAPACITY = 64;
constexpr size_t WEB_COMMAND_QUEUE_LENGTH = 8;
constexpr size_t DEBUG_EVENT_CAPACITY = 128;
constexpr uint32_t MAX_AUTOMATION_WEIGHT_AGE_MS = 1000;
constexpr float MIN_AUTOMATION_WEIGHT_G = -500.0f;
constexpr float MAX_AUTOMATION_WEIGHT_G = 1000.0f;
constexpr float MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S = 100.0f;
constexpr float AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G = 20.0f;
constexpr float POST_TARE_BASELINE_MAX_ABS_G = 50.0f;
constexpr uint32_t POST_TARE_BASELINE_GRACE_MS = 2000;
constexpr float MAX_PARSED_WEIGHT_G = 10000.0f;
constexpr uint8_t DIRECT_STOP_CONFIRMATION_SAMPLES = 2;
constexpr uint8_t WEIGHT_RECOVERY_CONFIRMATION_SAMPLES = 3;
constexpr uint32_t DIRECT_STOP_CONFIRMATION_WINDOW_MS = 1000;
constexpr float MAX_RECOVERY_WEIGHT_DROP_G = 2.0f;

#ifndef SHOT_STOPPER_ENABLE_REMOTE_CN9
#define SHOT_STOPPER_ENABLE_REMOTE_CN9 0
#endif

constexpr bool REMOTE_CN9_CONTROL_ENABLED =
    SHOT_STOPPER_ENABLE_REMOTE_CN9 == 1;
static_assert(SHOT_STOPPER_ENABLE_REMOTE_CN9 == 0 ||
                  SHOT_STOPPER_ENABLE_REMOTE_CN9 == 1,
              "SHOT_STOPPER_ENABLE_REMOTE_CN9 must be 0 or 1");

enum class StopperState : uint8_t {
  REQUIRES_OFF,
  READY,
  QUALIFYING_ON,
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
    case StopperState::QUALIFYING_ON: return "QUALIFYING_ON";
    case StopperState::BREW: return "BREW";
    case StopperState::RINSE: return "RINSE";
    case StopperState::MANUAL_NO_SCALE: return "MANUAL_NO_SCALE";
  }
  return "UNKNOWN";
}

enum class EndReason : uint8_t {
  NONE,
  PADDLE,
  SCALE_PREDICTION,
  SCALE_THRESHOLD,
  WEIGHT_ANOMALY,
  GLOBAL_LIMIT,
  CONFIGURED_WALL_LIMIT,
  SHORT_SHOT,
  RINSE_COMPLETE,
  WEB_STOP,
  PHYSICAL_OVERRIDE,
  WEB_HEARTBEAT_TIMEOUT,
  RELAY_SAFETY_FAILURE
};

struct RuntimeConfig {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  bool timerOnly = false;
  bool canTareStartTimer = true;
  // An independent Bookoo beep after Brew confirmation is optional.
  bool brewConfirmationBeep = true;
  // Remind the user to release the physical paddle after CN9 has opened.
  bool paddleReturnReminderBeep = true;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  uint32_t confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  int16_t timezoneOffsetMinutes = DEFAULT_TIMEZONE_OFFSET_MINUTES;
  uint8_t ntpServerPreset = static_cast<uint8_t>(NtpServerPreset::POOL);
  char ntpServerCustom[NTP_SERVER_HOST_CAPACITY] = {};
};

struct CycleConfigSnapshot {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  bool timerOnly = false;
  bool canTareStartTimer = true;
  bool brewConfirmationBeep = true;
  bool paddleReturnReminderBeep = true;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool autoRetare = true;
  uint32_t retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  float minimumCupWeightG = DEFAULT_MINIMUM_CUP_WEIGHT_G;
  uint8_t retareStabilitySamples = DEFAULT_RETARE_STABILITY_SAMPLES;
  float retareStabilityToleranceG = DEFAULT_RETARE_STABILITY_TOLERANCE_G;
  uint32_t retareStabilityMaxGapMs = DEFAULT_RETARE_STABILITY_MAX_GAP_MS;
  uint32_t retareStabilityMinDurationMs = DEFAULT_RETARE_STABILITY_MIN_DURATION_MS;
  uint32_t confirmationTimeoutMs = DEFAULT_CONFIRMATION_TIMEOUT_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
};

inline CycleConfigSnapshot snapshotConfig(const RuntimeConfig &config) {
  CycleConfigSnapshot snapshot;
  snapshot.revision = config.revision;
  snapshot.goalWeightG = config.goalWeightG;
  snapshot.weightOffsetG = config.weightOffsetG;
  snapshot.autoTare = config.autoTare;
  snapshot.timerOnly = config.timerOnly;
  snapshot.canTareStartTimer = config.canTareStartTimer;
  snapshot.brewConfirmationBeep = config.brewConfirmationBeep;
  snapshot.paddleReturnReminderBeep = config.paddleReturnReminderBeep;
  snapshot.paddleReturnReminderIntervalMs =
      config.paddleReturnReminderIntervalMs;
  snapshot.paddleReturnReminderMaxDurationMs =
      config.paddleReturnReminderMaxDurationMs;
  snapshot.rinseGestureMs = config.rinseGestureMs;
  snapshot.rinseDurationMs = config.rinseDurationMs;
  snapshot.autoRetare = config.autoRetare;
  snapshot.retareWindowMs = config.retareWindowMs;
  snapshot.minimumCupWeightG = config.minimumCupWeightG;
  snapshot.retareStabilitySamples = config.retareStabilitySamples;
  snapshot.retareStabilityToleranceG = config.retareStabilityToleranceG;
  snapshot.retareStabilityMaxGapMs = config.retareStabilityMaxGapMs;
  snapshot.retareStabilityMinDurationMs = config.retareStabilityMinDurationMs;
  snapshot.confirmationTimeoutMs = config.confirmationTimeoutMs;
  snapshot.operationalWallMs = config.operationalWallMs;
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
  CONFIRMATION_TIMEOUT,
  CONFIRMATION_RETARE_RELATION,
  OPERATIONAL_WALL,
  PADDLE_REMINDER_INTERVAL,
  PADDLE_REMINDER_MAX_DURATION,
  TIMING_RELATION,
  COMBINED_TARE_REQUIRES_AUTOTARE,
  TIMEZONE_OFFSET,
  NTP_SERVER_PRESET,
  NTP_SERVER_CUSTOM
};

inline uint32_t effectiveRetareWindowMs(const RuntimeConfig &config) {
  return config.autoRetare ? config.retareWindowMs : 0U;
}

inline uint32_t minimumConfirmationTimeoutMs(const RuntimeConfig &config) {
  return effectiveRetareWindowMs(config) + MIN_CONFIRMATION_AFTER_RETARE_MS;
}

inline uint32_t effectiveRetareWindowMs(const CycleConfigSnapshot &config) {
  return config.autoRetare ? config.retareWindowMs : 0U;
}

inline uint32_t minimumConfirmationTimeoutMs(
    const CycleConfigSnapshot &config) {
  return effectiveRetareWindowMs(config) + MIN_CONFIRMATION_AFTER_RETARE_MS;
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
  if (config.confirmationTimeoutMs < MIN_CONFIRMATION_TIMEOUT_MS ||
      config.confirmationTimeoutMs > MAX_CONFIRMATION_TIMEOUT_MS) {
    return ConfigValidationError::CONFIRMATION_TIMEOUT;
  }
  if (config.confirmationTimeoutMs <
      minimumConfirmationTimeoutMs(config)) {
    return ConfigValidationError::CONFIRMATION_RETARE_RELATION;
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
  if (!(config.rinseGestureMs < config.operationalWallMs) ||
      config.rinseDurationMs > config.operationalWallMs ||
      config.retareWindowMs > config.operationalWallMs ||
      config.confirmationTimeoutMs > config.operationalWallMs ||
      config.retareWindowMs + config.confirmationTimeoutMs >
          config.operationalWallMs) {
    return ConfigValidationError::TIMING_RELATION;
  }
  if (config.canTareStartTimer && !config.autoTare) {
    return ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE;
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
    case ConfigValidationError::CONFIRMATION_TIMEOUT:
      return "confirmationTimeoutMs";
    case ConfigValidationError::CONFIRMATION_RETARE_RELATION:
      return "confirmationRetareRelation";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "operationalWallMs";
    case ConfigValidationError::PADDLE_REMINDER_INTERVAL:
      return "paddleReturnReminderIntervalMs";
    case ConfigValidationError::PADDLE_REMINDER_MAX_DURATION:
      return "paddleReturnReminderMaxDurationMs";
    case ConfigValidationError::TIMING_RELATION: return "timingRelation";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "canTareStartTimer";
    case ConfigValidationError::TIMEZONE_OFFSET:
      return "timezoneOffsetMinutes";
    case ConfigValidationError::NTP_SERVER_PRESET:
      return "ntpServerPreset";
    case ConfigValidationError::NTP_SERVER_CUSTOM:
      return "ntpServerCustom";
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

inline bool validAccessPointPassword(const char *password) {
  return validWifiPassword(password, false);
}

enum class WebCommandType : uint8_t {
  PADDLE_ON,
  PADDLE_OFF,
  RINSE,
  STOP,
  STOP_HEARTBEAT,
  APPLY_CONFIG,
  RESET_WEIGHT_OFFSET,
  SAVE_NETWORK,
  FORGET_NETWORK,
  CHANGE_AP_PASSWORD,
  RESTART,
  RESET_NETWORK_UI,
  FACTORY_RESET,
  CLEAR_SHOT_LOG,
  PERSIST_RUNTIME,
  START_WIFI_SCAN,
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
    case WebCommandType::SAVE_NETWORK: return "save STA network";
    case WebCommandType::FORGET_NETWORK: return "forget STA network";
    case WebCommandType::CHANGE_AP_PASSWORD: return "change AP password";
    case WebCommandType::RESTART: return "restart";
    case WebCommandType::RESET_NETWORK_UI: return "recover network/UI";
    case WebCommandType::FACTORY_RESET: return "restore factory settings";
    case WebCommandType::CLEAR_SHOT_LOG: return "clear shot history";
    case WebCommandType::PERSIST_RUNTIME: return "persist workflow";
    case WebCommandType::START_WIFI_SCAN: return "scan Wi-Fi networks";
    case WebCommandType::MAINTENANCE_COMPLETE:
      return "maintenance result";
  }
  return "unknown web command";
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
  uint32_t maintenanceLeaseId = 0;
  uint32_t webSessionId = 0;
  uint32_t controlLeaseId = 0;
  RuntimeConfig config = {};
  char ssid[WIFI_SSID_CAPACITY] = {};
  char password[WIFI_PASSWORD_CAPACITY] = {};
  bool openNetwork = false;
  bool succeeded = false;
  CommandResultState resultState = CommandResultState::NONE;
};

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
  uint32_t webSessionId = 0;
  uint32_t controlLeaseId = 0;
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
  uint32_t scaleConnectionGeneration = 0;
  uint32_t scalePacketSequence = 0;
  uint32_t scalePacketGaps = 0;
  uint32_t scaleRejectedPackets = 0;
  uint32_t scaleReconnects = 0;
  uint8_t scaleLastDisconnectReason = 0;
  uint32_t loopMaxGapMs = 0;
  uint32_t loopStackMinWords = 0;
  uint32_t scaleStackMinWords = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minimumFreeHeapBytes = 0;
  uint32_t largestFreeHeapBlockBytes = 0;
  uint32_t scaleEventsDropped = 0;
  RuntimeConfig config = {};
  LastCycleSummary lastCycle = {};
  uint32_t debugEventsDropped = 0;
  bool cycleFlowDuringRetare = false;
  uint32_t cycleFirstDropMs = 0;
  uint32_t cycleRetareFlowFirstDetectedAtMs = 0;
  uint32_t cycleStartedAtMs = 0;
};

inline bool controlAllowsConfiguration(const ControlStatusSnapshot &status) {
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
  WEB
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
  CONFIG_ACCEPTED,
  CONFIG_REJECTED,
  WEIGHT_OFFSET_RESET,
  CONFIG_PERSISTED,
  CONFIG_MIGRATED,
  AP_STARTED,
  AP_STOPPED,
  STA_CONNECTING,
  STA_CONNECTED,
  STA_FAILED,
  WIFI_SCAN_STARTED,
  WIFI_SCAN_COMPLETE,
  WIFI_SCAN_ERROR,
  WIFI_SCAN_CANCELED,
  UI_LOGIN,
  UI_LOGOUT,
  UI_EXPIRED,
  UI_REPLACED,
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
  FIRST_DROP_DURING_RETARE
};

struct DebugEvent {
  uint32_t sequence = 0;
  uint32_t atMs = 0;
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

  void add(uint32_t atMs, DebugCategory category, DebugCode code,
           int32_t argument1 = 0, int32_t argument2 = 0) {
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

  uint32_t overwritten() const { return overwritten_; }

 private:
  DebugEvent events_[DEBUG_EVENT_CAPACITY] = {};
  uint32_t nextSequence_ = 1;
  size_t count_ = 0;
  size_t writeIndex_ = 0;
  uint32_t overwritten_ = 0;
};

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
    case DebugCode::SCALE_BEEP_OK: return "scale brew-confirmation beep sent";
    case DebugCode::SCALE_BEEP_FAILED:
      return "scale brew-confirmation beep failed";
    case DebugCode::SCALE_BEEP_UNSUPPORTED:
      return "scale has no state-safe beep command";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_OK:
      return "scale paddle-return reminder beep sent";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_FAILED:
      return "scale paddle-return reminder beep failed";
    case DebugCode::SCALE_PADDLE_REMINDER_BEEP_UNSUPPORTED:
      return "scale has no state-safe paddle-return reminder beep command";
    case DebugCode::CONFIG_ACCEPTED: return "configuration accepted";
    case DebugCode::CONFIG_REJECTED: return "configuration rejected";
    case DebugCode::WEIGHT_OFFSET_RESET:
      return "learned weight offset reset to default";
    case DebugCode::CONFIG_PERSISTED: return "configuration persisted";
    case DebugCode::CONFIG_MIGRATED: return "legacy configuration migrated";
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
    case DebugCode::UI_LOGIN: return "web session opened";
    case DebugCode::UI_LOGOUT: return "web session closed";
    case DebugCode::UI_EXPIRED: return "web session expired";
    case DebugCode::UI_REPLACED: return "web session replaced by newer login";
    case DebugCode::WEB_PADDLE_ON: return "paddle web on";
    case DebugCode::WEB_PADDLE_OFF: return "paddle web off";
    case DebugCode::WEB_RINSE: return "rinse web started";
    case DebugCode::WEB_COMMAND_ACCEPTED: return "web command accepted";
    case DebugCode::WEB_COMMAND_REJECTED: return "web command rejected";
    case DebugCode::WEB_STOP: return "web safe stop";
    case DebugCode::RESTART_REQUESTED: return "restart requested";
    case DebugCode::NETWORK_RESET: return "network settings reset";
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

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

}  // namespace shotstopper
