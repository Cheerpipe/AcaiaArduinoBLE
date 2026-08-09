#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t CONFIG_SCHEMA_VERSION = 4;
constexpr uint32_t PREVIOUS_CONFIG_SCHEMA_VERSION = 3;
constexpr uint32_t LEGACY_CONFIG_SCHEMA_VERSION = 2;
constexpr uint32_t HARD_MAX_CN9_CLOSED_MS = 50000;
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS = 50000;
constexpr uint32_t DEFAULT_RINSE_GESTURE_MS = 1500;
constexpr uint32_t DEFAULT_RINSE_DURATION_MS = 3000;
constexpr uint32_t DEFAULT_BREW_CONFIRM_MS = 3000;
constexpr uint32_t DEFAULT_MIN_AUTO_STOP_MS = 5000;
constexpr uint8_t MIN_GOAL_WEIGHT_G = 10;
constexpr uint8_t MAX_GOAL_WEIGHT_G = 200;
constexpr uint8_t DEFAULT_GOAL_WEIGHT_G = 36;
constexpr float MAX_OFFSET_G = 5.0f;
constexpr float DEFAULT_WEIGHT_OFFSET_G = 1.5f;
constexpr size_t WIFI_SSID_CAPACITY = 33;
constexpr size_t WIFI_PASSWORD_CAPACITY = 64;
constexpr size_t WEB_COMMAND_QUEUE_LENGTH = 8;
constexpr size_t DEBUG_EVENT_CAPACITY = 128;

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
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  uint32_t brewConfirmMs = DEFAULT_BREW_CONFIRM_MS;
  uint32_t minAutoStopMs = DEFAULT_MIN_AUTO_STOP_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
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
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  uint32_t brewConfirmMs = DEFAULT_BREW_CONFIRM_MS;
  uint32_t minAutoStopMs = DEFAULT_MIN_AUTO_STOP_MS;
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
  snapshot.rinseGestureMs = config.rinseGestureMs;
  snapshot.rinseDurationMs = config.rinseDurationMs;
  snapshot.brewConfirmMs = config.brewConfirmMs;
  snapshot.minAutoStopMs = config.minAutoStopMs;
  snapshot.operationalWallMs = config.operationalWallMs;
  return snapshot;
}

enum class ConfigValidationError : uint8_t {
  NONE,
  GOAL_WEIGHT,
  WEIGHT_OFFSET,
  RINSE_GESTURE,
  RINSE_DURATION,
  BREW_CONFIRM,
  MIN_AUTO_STOP,
  OPERATIONAL_WALL,
  TIMING_RELATION,
  COMBINED_TARE_REQUIRES_AUTOTARE
};

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
  if (config.brewConfirmMs < 500 || config.brewConfirmMs > 10000) {
    return ConfigValidationError::BREW_CONFIRM;
  }
  if (config.minAutoStopMs < 1000 || config.minAutoStopMs > 30000) {
    return ConfigValidationError::MIN_AUTO_STOP;
  }
  if (config.operationalWallMs < 5000 ||
      config.operationalWallMs > HARD_MAX_CN9_CLOSED_MS) {
    return ConfigValidationError::OPERATIONAL_WALL;
  }
  if (!(config.rinseGestureMs < config.brewConfirmMs &&
        config.brewConfirmMs < config.minAutoStopMs &&
        config.minAutoStopMs < config.operationalWallMs) ||
      config.rinseDurationMs > config.operationalWallMs) {
    return ConfigValidationError::TIMING_RELATION;
  }
  if (config.canTareStartTimer && !config.autoTare) {
    return ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE;
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
    case ConfigValidationError::BREW_CONFIRM: return "brewConfirmMs";
    case ConfigValidationError::MIN_AUTO_STOP: return "minAutoStopMs";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "operationalWallMs";
    case ConfigValidationError::TIMING_RELATION: return "timingRelation";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "canTareStartTimer";
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
  PERSIST_RUNTIME
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
    case WebCommandType::PERSIST_RUNTIME: return "persist workflow";
  }
  return "unknown web command";
}

struct WebCommand {
  WebCommandType type = WebCommandType::STOP;
  uint32_t requestId = 0;
  RuntimeConfig config = {};
  char ssid[WIFI_SSID_CAPACITY] = {};
  char password[WIFI_PASSWORD_CAPACITY] = {};
  bool openNetwork = false;
};

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
};

struct ControlStatusSnapshot {
  StopperState state = StopperState::REQUIRES_OFF;
  bool activeCycle = false;
  bool relayClosed = false;
  bool physicalPaddleOn = false;
  bool virtualPaddleOn = false;
  ControlSource source = ControlSource::NONE;
  uint32_t cycleId = 0;
  uint32_t cn9ElapsedMs = 0;
  bool scaleAvailable = false;
  bool currentWeightValid = false;
  float currentWeightG = 0.0f;
  uint32_t currentWeightAgeMs = 0;
  RuntimeConfig config = {};
  LastCycleSummary lastCycle = {};
  uint32_t debugEventsDropped = 0;
};

inline bool controlAllowsConfiguration(const ControlStatusSnapshot &status) {
  return status.state == StopperState::READY && !status.activeCycle &&
         !status.relayClosed && !status.physicalPaddleOn;
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
  FACTORY_RESET
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
  }
  return "unknown";
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
