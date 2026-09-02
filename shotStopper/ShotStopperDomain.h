#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ShotStopperSafety.h"
#include "ShotStopperHwmon.h"

// Map ESP-IDF Kconfig (main/Kconfig.projbuild) onto the historical
// SHOT_STOPPER_* macros. Command-line -DSHOT_STOPPER_*=… wins (ifndef).
#ifndef SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL
#ifdef SHOT_STOPPER_ENABLE_REMOTE_CN9
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL SHOT_STOPPER_ENABLE_REMOTE_CN9
#endif
#endif
#if !defined(SHOT_STOPPER_HOST_TEST) && __has_include("sdkconfig.h")
#include "sdkconfig.h"
#ifndef SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL
#ifdef CONFIG_SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 1
#elif defined(CONFIG_SHOT_STOPPER_ENABLE_REMOTE_CN9)
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 1
#else
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 0
#endif
#endif
#ifndef SHOT_STOPPER_ENABLE_BUZZER
#ifdef CONFIG_SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER CONFIG_SHOT_STOPPER_ENABLE_BUZZER
#endif
#endif
#ifndef SHOT_STOPPER_MACHINE_TYPE
#ifdef CONFIG_SHOT_STOPPER_MACHINE_TYPE
#define SHOT_STOPPER_MACHINE_TYPE CONFIG_SHOT_STOPPER_MACHINE_TYPE
#endif
#endif
#ifndef SHOT_STOPPER_DEVELOPMENT
#ifdef CONFIG_SHOT_STOPPER_DEVELOPMENT
#define SHOT_STOPPER_DEVELOPMENT 1
#else
#define SHOT_STOPPER_DEVELOPMENT 0
#endif
#endif
#ifndef SHOT_STOPPER_ENABLE_JTAG
#ifdef CONFIG_SHOT_STOPPER_ENABLE_JTAG
#define SHOT_STOPPER_ENABLE_JTAG CONFIG_SHOT_STOPPER_ENABLE_JTAG
#endif
#endif
#endif

#include "ShotStopperMachineTypes.h"
#include "ShotStopperMachinePaddleConfig.h"
#include "ShotStopperScaleTypes.h"
#include "ShotStopperBrewTypes.h"

namespace shotstopper {

constexpr uint32_t SERIAL_BAUD = 115200;
// Current persisted settings schema. V1 is the 1912-byte baseline (padding
// after staOpen). V2 names that byte staWifiSleep without growing the blob.
// Bump and add a migration when the blob layout changes
// (see ShotStopperSettingsMigrate.h).
constexpr uint32_t CONFIG_SCHEMA_VERSION = 6;
// Rinse clock default. Detection window default is DEFAULT_RINSE_GESTURE_MS
// (machine-owned, ShotStopperMachineTypes.h).
constexpr uint32_t DEFAULT_RINSE_DURATION_MS = 4000;

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

enum class NoScaleBbwMode : uint8_t {
  OFF = 0,
  WARN_ONCE = 1,
  REQUIRE_SCALE = 2
};

inline bool validNoScaleBbwMode(uint8_t mode) {
  return mode <= static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
}

inline bool noScaleBbwEnabled(uint8_t mode) {
  return mode != static_cast<uint8_t>(NoScaleBbwMode::OFF);
}

inline bool noScaleBbwRequiresScale(uint8_t mode) {
  return mode == static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
}

inline const char *noScaleBbwModeId(uint8_t mode) {
  switch (static_cast<NoScaleBbwMode>(mode)) {
    case NoScaleBbwMode::OFF: return "off";
    case NoScaleBbwMode::WARN_ONCE: return "warn_once";
    case NoScaleBbwMode::REQUIRE_SCALE: return "require_scale";
  }
  return "off";
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
constexpr size_t WIFI_SSID_CAPACITY = 33;
constexpr size_t WIFI_PASSWORD_CAPACITY = 64;
constexpr size_t WEB_COMMAND_QUEUE_LENGTH = 4;
constexpr size_t DEBUG_EVENT_CAPACITY = 96;

#ifndef SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL
#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 0
#endif

constexpr bool REMOTE_MACHINE_CONTROL_ENABLED =
    SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL == 1;
static_assert(SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL == 0 ||
                  SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL == 1,
              "SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL must be 0 or 1");

#ifndef SHOT_STOPPER_ENABLE_BUZZER
#define SHOT_STOPPER_ENABLE_BUZZER 0
#endif

constexpr bool BUZZER_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_BUZZER != 0;
static_assert(SHOT_STOPPER_ENABLE_BUZZER == 0 ||
                  SHOT_STOPPER_ENABLE_BUZZER == 1,
              "SHOT_STOPPER_ENABLE_BUZZER must be 0 (off) or 1 (passive RTTTL)");

inline const char *compiledBuzzerModeId() {
  if (SHOT_STOPPER_ENABLE_BUZZER == 1) {
    return "passive";
  }
  return "off";
}

#ifndef SHOT_STOPPER_DEVELOPMENT
#define SHOT_STOPPER_DEVELOPMENT 0
#endif

constexpr bool DEVELOPMENT_BUILD = SHOT_STOPPER_DEVELOPMENT == 1;
static_assert(SHOT_STOPPER_DEVELOPMENT == 0 || SHOT_STOPPER_DEVELOPMENT == 1,
              "SHOT_STOPPER_DEVELOPMENT must be 0 or 1");

#ifndef SHOT_STOPPER_ENABLE_JTAG
#define SHOT_STOPPER_ENABLE_JTAG 0
#endif

constexpr bool JTAG_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_JTAG == 1;
static_assert(SHOT_STOPPER_ENABLE_JTAG == 0 || SHOT_STOPPER_ENABLE_JTAG == 1,
              "SHOT_STOPPER_ENABLE_JTAG must be 0 (off) or 1 (USB Serial/JTAG)");

// Why USB CDC/Serial.begin ran at boot (latch). Live IO4 is separate.
// Avoid JTAG/IO4/DISABLED: Arduino-ESP32 defines those as macros
// (pins / esp32-hal-gpio.h), which would break this enum mid-parse.
enum class UsbSerialEnableSource : uint8_t {
  OFF = 0,
  COMPILE_FLAG = 1,
  JUMPER = 2,
};

inline const char *usbSerialStateId(UsbSerialEnableSource source) {
  switch (source) {
  case UsbSerialEnableSource::COMPILE_FLAG:
    return "enabled_jtag";
  case UsbSerialEnableSource::JUMPER:
    return "enabled_io4";
  case UsbSerialEnableSource::OFF:
  default:
    return "disabled";
  }
}

inline const char *usbConsoleIo4StateId(bool closed) {
  return closed ? "closed" : "open";
}

#ifndef SHOT_STOPPER_MACHINE_TYPE
#define SHOT_STOPPER_MACHINE_TYPE 0
#endif
#ifndef SHOT_STOPPER_STOP_PULSE_MS
#define SHOT_STOPPER_STOP_PULSE_MS 300
#endif
#ifndef SHOT_STOPPER_MAX_SINGLE_PRESS_MS
#define SHOT_STOPPER_MAX_SINGLE_PRESS_MS 1000
#endif
#ifndef SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS
#define SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS 1000
#endif

static_assert(SHOT_STOPPER_MACHINE_TYPE >= 0 && SHOT_STOPPER_MACHINE_TYPE <= 2,
              "SHOT_STOPPER_MACHINE_TYPE must be 0 (paddle/latch), 1 (momentary), "
              "or 2 (momentary+reed)");
static_assert(SHOT_STOPPER_STOP_PULSE_MS >= 50 &&
                  SHOT_STOPPER_STOP_PULSE_MS <= 1000,
              "SHOT_STOPPER_STOP_PULSE_MS must be 50–1000");
static_assert(SHOT_STOPPER_MAX_SINGLE_PRESS_MS >= 100 &&
                  SHOT_STOPPER_MAX_SINGLE_PRESS_MS <= 5000,
              "SHOT_STOPPER_MAX_SINGLE_PRESS_MS must be 100–5000");
static_assert(SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS >= 200 &&
                  SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS <= 5000,
              "SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS must be 200–5000");

enum class MachineType : uint8_t {
  PADDLE = 0,
  MOMENTARY = 1,
  MOMENTARY_REED = 2
};

constexpr uint8_t COMPILED_MACHINE_TYPE =
    static_cast<uint8_t>(SHOT_STOPPER_MACHINE_TYPE);
constexpr bool MACHINE_USES_MOMENTARY_SWITCH = SHOT_STOPPER_MACHINE_TYPE != 0;
constexpr bool MACHINE_HAS_REED = SHOT_STOPPER_MACHINE_TYPE == 2;
constexpr uint32_t COMPILED_STOP_PULSE_MS =
    static_cast<uint32_t>(SHOT_STOPPER_STOP_PULSE_MS);
constexpr uint32_t COMPILED_MAX_SINGLE_PRESS_MS =
    static_cast<uint32_t>(SHOT_STOPPER_MAX_SINGLE_PRESS_MS);
constexpr uint32_t COMPILED_REED_CONFIRM_TIMEOUT_MS =
    static_cast<uint32_t>(SHOT_STOPPER_REED_CONFIRM_TIMEOUT_MS);
constexpr uint32_t MIN_REED_CONFIRM_TIMEOUT_MS = 200;
constexpr uint32_t MAX_REED_CONFIRM_TIMEOUT_MS = 5000;
constexpr uint32_t DEFAULT_REED_CONFIRM_TIMEOUT_MS = 1000;
constexpr uint32_t COMPILED_SHOT_REACT_TIMEOUT_MS = 12000;
constexpr uint8_t MIN_SHOT_REACT_TIMEOUT_S = 3;
constexpr uint8_t MAX_SHOT_REACT_TIMEOUT_S = 30;
constexpr uint8_t DEFAULT_SHOT_REACT_TIMEOUT_S = 12;

inline const char *compiledMachineTypeId() {
  switch (SHOT_STOPPER_MACHINE_TYPE) {
    case 1:
      return "momentary";
    case 2:
      return "momentary_reed";
    default:
      return "paddle";
  }
}

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


enum class LogLevel : uint8_t {
  CRITICAL = 0,
  ERROR = 1,
  WARNING = 2,
  INFO = 3,
  DEBUG = 4,
  NONE = 5
};

// NVS/UI compose of Machine + Scale + Brew settings. Baseline schema is V1;
// do not change this blob layout without bumping CONFIG_SCHEMA_VERSION.
// New fields: consider debug export (ShotStopperDebugExport.h).
struct RuntimeConfig {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  // Seed for Reset learned stop offset; factory default remains 1.5 g.
  float weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  uint32_t postTareBaselineGraceMs = DEFAULT_POST_TARE_BASELINE_GRACE_MS;
  // Internal polarity: true disables weight stop. UI/API brewByWeight is the inverse.
  bool timerOnly = false;
  bool canTareStartTimer = true;
  // Optional pad after the scale timer catches up to circuit whole seconds.
  uint32_t scaleTimerStopExtraDelayMs = DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS;
  // Optional beep when the first coffee drop is detected.
  bool firstDropBeep = true;
  // Remind the user to release the physical paddle after machine circuit has opened.
  bool paddleReturnReminderBeep = true;
  bool soundAlertsMuted = false;
  uint32_t paddleReturnReminderIntervalMs =
      DEFAULT_PADDLE_RETURN_REMINDER_INTERVAL_MS;
  uint32_t paddleReturnReminderMaxDurationMs =
      DEFAULT_PADDLE_RETURN_REMINDER_MAX_DURATION_MS;
  uint8_t paddleMode = static_cast<uint8_t>(PaddleMode::NATURAL);
  // Local buzzer alerts (active when SHOT_STOPPER_ENABLE_BUZZER is 1).
  bool buzzerScaleLostBeep = true;
  bool buzzerAutoToManualGuardEndBeep = true;
  bool buzzerManualNoScaleBeep = true;
  bool buzzerScaleConnectedBeep = true;
  // Onboard GPIO LED: solid when a BLE scale is connected, fast blink while
  // connecting, slow blink while the weight stream is stale.
  bool scaleConnectedLed = true;
  uint8_t buzzerExtendedPulseRate =
      static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE);
  uint8_t buzzerSlowExtendedPulseRate =
      static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE);
  // scale_only | buzzer_only | scale_priority (ignored when buzzer not compiled).
  // Default: buzzer_only with SHOT_STOPPER_ENABLE_BUZZER, else scale_only.
  uint8_t alertOutputChannel =
      static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL);
  uint32_t rinseGestureMs = DEFAULT_RINSE_GESTURE_MS;  // machine-owned detection
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;  // rinse clock
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
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBbwBrewTimeMs = DEFAULT_MAX_BBW_BREW_TIME_MS;
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
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  bool bookooMuteOnBuzzerOnly = true;
  uint8_t bookooConnectBeepLevel = DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  bool avoidAccidentalTouchEnabled = true;
  // Dead field: cup presence uses minimumCupWeightG. Kept for NVS layout.
  float cupPresentWeightG = DEFAULT_CUP_PRESENT_WEIGHT_G;
  float cupRemovedWeightG = DEFAULT_CUP_REMOVED_WEIGHT_G;
  // Reuses the legacy avoidBbwShotWithoutScale byte: false=OFF, true=WARN_ONCE.
  uint8_t noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
  uint32_t lastShotCooldownMs = DEFAULT_LAST_SHOT_COOLDOWN_MS;
  // USB debug spew (paddle/machine circuit/Wi-Fi traces). CLI replies stay independent.
  bool serialDebugOutput = false;
  // Minimum level retained in the RAM debug ring (WebUI Log). NONE disables.
  uint8_t ringRetainLogLevel = static_cast<uint8_t>(LogLevel::NONE);
  // Momentary Switch timings packed to keep NVS size. 0 = compiled default
  // (legacy blobs that stored momentaryStartOnPress in stopPulseTenMs).
  uint8_t stopPulseTenMs =
      static_cast<uint8_t>((SHOT_STOPPER_STOP_PULSE_MS + 5) / 10);
  uint8_t maxSinglePressHundredMs =
      static_cast<uint8_t>((SHOT_STOPPER_MAX_SINGLE_PRESS_MS + 50) / 100);
  // Wait after shot end before capturing the post-drip weight.
  uint32_t dripDelayMs = DEFAULT_DRIP_DELAY_MS;
  // Start/stop on press (default) vs release. Reed confirm window.
  bool momentaryStartOnPress = true;
  uint8_t reedConfirmTimeoutHundredMs = 0;
  // Switch-only inferred-state sync. 0 timeout = compiled 12 s default.
  bool assumeIdleWhenScaleConnects = true;
  uint8_t shotReactTimeoutS = 0;
  // Firmware rinse on/off. Default off for every machine type.
  bool rinseEnabled = false;
  // Makes the read-only Diagnostic view available without an Admin unlock.
  // This occupies a former trailing padding byte, preserving the blob size.
  bool showDiagnosticPage = false;
};

static_assert(sizeof(RuntimeConfig) == 252,
              "RuntimeConfig NVS size changed; bump CONFIG_SCHEMA_VERSION");
static_assert(offsetof(RuntimeConfig, stopPulseTenMs) == 238,
              "RuntimeConfig stopPulseTenMs offset changed");
static_assert(offsetof(RuntimeConfig, dripDelayMs) == 240,
              "RuntimeConfig dripDelayMs offset changed");
static_assert(offsetof(RuntimeConfig, momentaryStartOnPress) == 244,
              "RuntimeConfig momentaryStartOnPress offset changed");
static_assert(offsetof(RuntimeConfig, reedConfirmTimeoutHundredMs) == 245,
              "RuntimeConfig reedConfirmTimeoutHundredMs offset changed");
static_assert(offsetof(RuntimeConfig, assumeIdleWhenScaleConnects) == 246,
              "RuntimeConfig assumeIdleWhenScaleConnects offset changed");
static_assert(offsetof(RuntimeConfig, shotReactTimeoutS) == 247,
              "RuntimeConfig shotReactTimeoutS offset changed");
static_assert(offsetof(RuntimeConfig, rinseEnabled) == 248,
              "RuntimeConfig rinseEnabled offset changed");

inline uint32_t runtimeStopPulseMs(const RuntimeConfig &config) {
  if (config.stopPulseTenMs == 0) {
    return COMPILED_STOP_PULSE_MS;
  }
  const uint32_t ms = static_cast<uint32_t>(config.stopPulseTenMs) * 10U;
  if (ms < 50U || ms > 1000U) {
    return COMPILED_STOP_PULSE_MS;
  }
  return ms;
}

inline uint32_t runtimeMaxSinglePressMs(const RuntimeConfig &config) {
  if (config.maxSinglePressHundredMs == 0) {
    return COMPILED_MAX_SINGLE_PRESS_MS;
  }
  const uint32_t ms =
      static_cast<uint32_t>(config.maxSinglePressHundredMs) * 100U;
  if (ms < 100U || ms > 5000U) {
    return COMPILED_MAX_SINGLE_PRESS_MS;
  }
  return ms;
}

inline void setRuntimeStopPulseMs(RuntimeConfig &config, uint32_t ms) {
  if (ms < 50U) {
    ms = 50U;
  }
  if (ms > 1000U) {
    ms = 1000U;
  }
  config.stopPulseTenMs = static_cast<uint8_t>((ms + 5U) / 10U);
}

inline void setRuntimeMaxSinglePressMs(RuntimeConfig &config, uint32_t ms) {
  if (ms < 100U) {
    ms = 100U;
  }
  if (ms > 5000U) {
    ms = 5000U;
  }
  config.maxSinglePressHundredMs = static_cast<uint8_t>((ms + 50U) / 100U);
}

inline uint32_t runtimeReedConfirmTimeoutMs(const RuntimeConfig &config) {
  if (config.reedConfirmTimeoutHundredMs == 0) {
    return COMPILED_REED_CONFIRM_TIMEOUT_MS;
  }
  const uint32_t ms =
      static_cast<uint32_t>(config.reedConfirmTimeoutHundredMs) * 100U;
  if (ms < MIN_REED_CONFIRM_TIMEOUT_MS || ms > MAX_REED_CONFIRM_TIMEOUT_MS) {
    return COMPILED_REED_CONFIRM_TIMEOUT_MS;
  }
  return ms;
}

inline void setRuntimeReedConfirmTimeoutMs(RuntimeConfig &config, uint32_t ms) {
  if (ms < MIN_REED_CONFIRM_TIMEOUT_MS) {
    ms = MIN_REED_CONFIRM_TIMEOUT_MS;
  }
  if (ms > MAX_REED_CONFIRM_TIMEOUT_MS) {
    ms = MAX_REED_CONFIRM_TIMEOUT_MS;
  }
  config.reedConfirmTimeoutHundredMs =
      static_cast<uint8_t>((ms + 50U) / 100U);
}

inline uint32_t runtimeShotReactTimeoutMs(const RuntimeConfig &config) {
  if (config.shotReactTimeoutS == 0) {
    return COMPILED_SHOT_REACT_TIMEOUT_MS;
  }
  if (config.shotReactTimeoutS < MIN_SHOT_REACT_TIMEOUT_S ||
      config.shotReactTimeoutS > MAX_SHOT_REACT_TIMEOUT_S) {
    return COMPILED_SHOT_REACT_TIMEOUT_MS;
  }
  return static_cast<uint32_t>(config.shotReactTimeoutS) * 1000U;
}

inline uint8_t runtimeShotReactTimeoutS(const RuntimeConfig &config) {
  return static_cast<uint8_t>(runtimeShotReactTimeoutMs(config) / 1000U);
}

struct CycleConfigSnapshot {
  uint32_t revision = 1;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool autoTare = true;
  uint32_t postTareBaselineGraceMs = DEFAULT_POST_TARE_BASELINE_GRACE_MS;
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
  uint32_t rinseDurationMs = DEFAULT_RINSE_DURATION_MS;
  bool rinseEnabled = false;
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
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBbwBrewTimeMs = DEFAULT_MAX_BBW_BREW_TIME_MS;
  bool autoToManualGuardEnabled = true;
  uint8_t autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  uint32_t autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  bool avoidAccidentalTouchEnabled = true;
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
  snapshot.postTareBaselineGraceMs = config.postTareBaselineGraceMs;
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
  snapshot.rinseDurationMs = config.rinseDurationMs;
  snapshot.rinseEnabled = config.rinseEnabled;
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
  snapshot.minBbwBrewTimeMs = config.minBbwBrewTimeMs;
  snapshot.slowExtractionGuardEnabled = config.slowExtractionGuardEnabled;
  snapshot.minRecoveryWeightG = config.minRecoveryWeightG;
  snapshot.maxBbwBrewTimeMs = config.maxBbwBrewTimeMs;
  snapshot.autoToManualGuardEnabled = config.autoToManualGuardEnabled;
  snapshot.autoToManualGuardLimitMode = config.autoToManualGuardLimitMode;
  snapshot.autoToManualGuardManualLimitMs =
      config.autoToManualGuardManualLimitMs;
  snapshot.cupProtectionEnabled = config.cupProtectionEnabled;
  snapshot.stopIfCupRemoved = config.stopIfCupRemoved;
  snapshot.requireCupToStart = config.requireCupToStart;
  snapshot.avoidAccidentalTouchEnabled = config.avoidAccidentalTouchEnabled;
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
  POST_TARE_BASELINE_GRACE,
  SCALE_TIMER_STOP_EXTRA_DELAY,
  TIMEZONE_OFFSET,
  NTP_SERVER_PRESET,
  NTP_SERVER_CUSTOM,
  MAX_RECOVERY_WEIGHT,
  MIN_BBW_BREW_TIME,
  FAST_EXTRACTION_GUARD_RELATION,
  MIN_RECOVERY_WEIGHT,
  MAX_BBW_BREW_TIME,
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
  NO_SCALE_BBW_MODE,
  LAST_SHOT_COOLDOWN,
  DRIP_DELAY,
  RING_RETAIN_LOG_LEVEL,
  PADDLE_MODE,
  CUP_PRESENT_WEIGHT,
  CUP_REMOVED_WEIGHT,
  STOP_PULSE,
  MAX_SINGLE_PRESS,
  REED_CONFIRM_TIMEOUT,
  SHOT_REACT_TIMEOUT
};

constexpr size_t MAX_SHOT_PRESETS = 8;
constexpr size_t SHOT_PRESET_NAME_CAPACITY = 24;
constexpr uint8_t FACTORY_PRESET_ID_SINGLE = 1;
constexpr uint8_t FACTORY_PRESET_ID_DOUBLE = 2;
constexpr float FACTORY_SINGLE_WEIGHT_OFFSET_G = 0.5f;
constexpr uint8_t FACTORY_SINGLE_GOAL_WEIGHT_G = 18;
constexpr float FACTORY_SINGLE_MAX_RECOVERY_WEIGHT_G = 20.0f;
constexpr uint32_t FACTORY_SINGLE_MIN_BBW_BREW_TIME_MS = 28000;
constexpr float FACTORY_SINGLE_MIN_RECOVERY_WEIGHT_G = 16.0f;
constexpr uint32_t FACTORY_SINGLE_MAX_BBW_BREW_TIME_MS = 44000;

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
  if (runtime.maxBbwBrewTimeMs >= runtime.operationalWallMs) {
    runtime.maxBbwBrewTimeMs = runtime.operationalWallMs > 1000
                                ? runtime.operationalWallMs - 1000
                                : DEFAULT_MAX_BBW_BREW_TIME_MS;
  }
  if (runtime.maxBbwBrewTimeMs < runtime.bbwProtectionMs) {
    runtime.maxBbwBrewTimeMs = runtime.bbwProtectionMs;
  }
  if (runtime.fastExtractionGuardEnabled &&
      runtime.maxBbwBrewTimeMs <= runtime.minBbwBrewTimeMs) {
    const uint32_t repaired = runtime.minBbwBrewTimeMs + 1000;
    if (repaired < runtime.operationalWallMs &&
        repaired <= MAX_MAX_BBW_BREW_TIME_MS) {
      runtime.maxBbwBrewTimeMs = repaired;
    } else {
      runtime.slowExtractionGuardEnabled = false;
    }
  }
  if (runtime.slowExtractionGuardEnabled &&
      (!isfinite(runtime.minRecoveryWeightG) ||
       runtime.minRecoveryWeightG < MIN_MIN_RECOVERY_WEIGHT_G ||
       runtime.minRecoveryWeightG > MAX_MIN_RECOVERY_WEIGHT_G ||
       runtime.minRecoveryWeightG >= static_cast<float>(runtime.goalWeightG) ||
       runtime.maxBbwBrewTimeMs < MIN_MAX_BBW_BREW_TIME_MS ||
       runtime.maxBbwBrewTimeMs > MAX_MAX_BBW_BREW_TIME_MS ||
       runtime.maxBbwBrewTimeMs >= runtime.operationalWallMs ||
       runtime.maxBbwBrewTimeMs < runtime.bbwProtectionMs ||
       (runtime.fastExtractionGuardEnabled &&
        runtime.maxBbwBrewTimeMs <= runtime.minBbwBrewTimeMs))) {
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
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBbwBrewTimeMs = DEFAULT_MAX_BBW_BREW_TIME_MS;
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
  bool avoidAccidentalTouchEnabled = true;
  // Unused leftover: cup mass lives on RuntimeConfig. Kept for NVS layout.
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
      config.operationalWallMs > HARD_MAX_CIRCUIT_CLOSED_MS) {
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
  // must fit under the machine circuit wall (do not sum them).
  if (!(config.rinseGestureMs < config.operationalWallMs) ||
      config.rinseDurationMs > config.operationalWallMs ||
      config.retareWindowMs > config.operationalWallMs ||
      config.bbwProtectionMs > config.operationalWallMs) {
    return ConfigValidationError::TIMING_RELATION;
  }
  if (config.canTareStartTimer && !config.autoTare) {
    return ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE;
  }
  if (config.postTareBaselineGraceMs < MIN_POST_TARE_BASELINE_GRACE_MS ||
      config.postTareBaselineGraceMs > MAX_POST_TARE_BASELINE_GRACE_MS) {
    return ConfigValidationError::POST_TARE_BASELINE_GRACE;
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
  if (!validNoScaleBbwMode(config.noScaleBbwMode)) {
    return ConfigValidationError::NO_SCALE_BBW_MODE;
  }
  if (config.lastShotCooldownMs < MIN_LAST_SHOT_COOLDOWN_MS ||
      config.lastShotCooldownMs > MAX_LAST_SHOT_COOLDOWN_MS) {
    return ConfigValidationError::LAST_SHOT_COOLDOWN;
  }
  if (config.dripDelayMs < MIN_DRIP_DELAY_MS ||
      config.dripDelayMs > MAX_DRIP_DELAY_MS) {
    return ConfigValidationError::DRIP_DELAY;
  }
  if (config.stopPulseTenMs != 0 &&
      (config.stopPulseTenMs < 5 || config.stopPulseTenMs > 100)) {
    return ConfigValidationError::STOP_PULSE;
  }
  if (config.maxSinglePressHundredMs != 0 &&
      (config.maxSinglePressHundredMs < 1 ||
       config.maxSinglePressHundredMs > 50)) {
    return ConfigValidationError::MAX_SINGLE_PRESS;
  }
  if (config.reedConfirmTimeoutHundredMs != 0 &&
      (config.reedConfirmTimeoutHundredMs < 2 ||
       config.reedConfirmTimeoutHundredMs > 50)) {
    return ConfigValidationError::REED_CONFIRM_TIMEOUT;
  }
  if (config.shotReactTimeoutS != 0 &&
      (config.shotReactTimeoutS < MIN_SHOT_REACT_TIMEOUT_S ||
       config.shotReactTimeoutS > MAX_SHOT_REACT_TIMEOUT_S)) {
    return ConfigValidationError::SHOT_REACT_TIMEOUT;
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
    if (config.minBbwBrewTimeMs < MIN_MIN_BBW_BREW_TIME_MS ||
        config.minBbwBrewTimeMs > MAX_MIN_BBW_BREW_TIME_MS) {
      return ConfigValidationError::MIN_BBW_BREW_TIME;
    }
    if (config.maxRecoveryWeightG <= static_cast<float>(config.goalWeightG) ||
        config.minBbwBrewTimeMs >= config.operationalWallMs ||
        config.minBbwBrewTimeMs < config.bbwProtectionMs) {
      return ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION;
    }
  }
  if (config.slowExtractionGuardEnabled) {
    if (!isfinite(config.minRecoveryWeightG) ||
        config.minRecoveryWeightG < MIN_MIN_RECOVERY_WEIGHT_G ||
        config.minRecoveryWeightG > MAX_MIN_RECOVERY_WEIGHT_G) {
      return ConfigValidationError::MIN_RECOVERY_WEIGHT;
    }
    if (config.maxBbwBrewTimeMs < MIN_MAX_BBW_BREW_TIME_MS ||
        config.maxBbwBrewTimeMs > MAX_MAX_BBW_BREW_TIME_MS) {
      return ConfigValidationError::MAX_BBW_BREW_TIME;
    }
    if (config.minRecoveryWeightG >= static_cast<float>(config.goalWeightG) ||
        config.maxBbwBrewTimeMs >= config.operationalWallMs ||
        config.maxBbwBrewTimeMs < config.bbwProtectionMs ||
        (config.fastExtractionGuardEnabled &&
         config.maxBbwBrewTimeMs <= config.minBbwBrewTimeMs)) {
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
    case ConfigValidationError::POST_TARE_BASELINE_GRACE:
      return "postTareBaselineGraceMs";
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
    case ConfigValidationError::MIN_BBW_BREW_TIME:
      return "minBbwBrewTimeMs";
    case ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION:
      return "fastExtractionGuardRelation";
    case ConfigValidationError::MIN_RECOVERY_WEIGHT:
      return "minRecoveryWeightG";
    case ConfigValidationError::MAX_BBW_BREW_TIME:
      return "maxBbwBrewTimeMs";
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
    case ConfigValidationError::NO_SCALE_BBW_MODE:
      return "noScaleBbwMode";
    case ConfigValidationError::LAST_SHOT_COOLDOWN:
      return "lastShotCooldownMs";
    case ConfigValidationError::DRIP_DELAY:
      return "dripDelayMs";
    case ConfigValidationError::STOP_PULSE:
      return "stopPulseMs";
    case ConfigValidationError::MAX_SINGLE_PRESS:
      return "maxSinglePressMs";
    case ConfigValidationError::REED_CONFIRM_TIMEOUT:
      return "reedConfirmTimeoutMs";
    case ConfigValidationError::SHOT_REACT_TIMEOUT:
      return "shotReactTimeoutS";
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

// Host-testable Wi-Fi modem-sleep policy. Device maps NONE/MIN_MODEM onto
// WIFI_PS_NONE / WIFI_PS_MIN_MODEM. Never MAX_MODEM or WIFI_OFF.
// Admin "Wi-Fi sleep" is sticky: scale connect/GATT-up does not flip PS.
enum class WifiPowerSaveMode : uint8_t { NONE = 0, MIN_MODEM = 1 };

inline WifiPowerSaveMode desiredWifiPowerSave(bool sleepAllowed, bool apActive,
                                              bool staAssociated,
                                              bool otaBusy) {
  if (!sleepAllowed || apActive || !staAssociated || otaBusy) {
    return WifiPowerSaveMode::NONE;
  }
  return WifiPowerSaveMode::MIN_MODEM;
}

// Settings / shot-store NVS flushes. Defer while a shot (or machine circuit)
// is busy or the scale is mid-connect; dirty state stays in RAM.
inline bool durableFlashWriteAllowed(bool shotOrMachineBusy,
                                     bool scaleConnecting) {
  return !shotOrMachineBusy && !scaleConnecting;
}

// Live STA power-save reported by the driver (diagnostic). May be MAX_MODEM
// if IDF restored it; applyWifiPowerSave never requests that.
enum class WifiPsLive : uint8_t {
  UNKNOWN = 0,
  NONE = 1,
  MIN_MODEM = 2,
  MAX_MODEM = 3
};

inline const char *wifiPsLiveName(WifiPsLive ps) {
  switch (ps) {
    case WifiPsLive::NONE:
      return "NONE";
    case WifiPsLive::MIN_MODEM:
      return "MIN_MODEM";
    case WifiPsLive::MAX_MODEM:
      return "MAX_MODEM";
    default:
      return "UNKNOWN";
  }
}

// One scanned BSS for strongest-AP selection (same SSID, many BSSIDs).
struct StaApScanEntry {
  const char *ssid = nullptr;
  const uint8_t *bssid = nullptr;
  uint8_t channel = 0;
  int32_t rssi = 0;
  bool open = false;
};

// Picks the matching SSID/auth entry with the highest RSSI. Does not collapse
// by SSID — callers pass one entry per BSSID.
inline bool selectBestStaAp(const StaApScanEntry *entries, size_t count,
                            const char *targetSsid, bool openNetwork,
                            size_t &bestIndexOut) {
  if (entries == nullptr || targetSsid == nullptr || !validWifiSsid(targetSsid)) {
    return false;
  }
  bool found = false;
  size_t bestIndex = 0;
  int32_t bestRssi = 0;
  for (size_t index = 0; index < count; ++index) {
    const StaApScanEntry &entry = entries[index];
    if (entry.ssid == nullptr || entry.bssid == nullptr) {
      continue;
    }
    if (strcmp(entry.ssid, targetSsid) != 0) {
      continue;
    }
    if (entry.open != openNetwork) {
      continue;
    }
    if (!found || entry.rssi > bestRssi) {
      found = true;
      bestIndex = index;
      bestRssi = entry.rssi;
    }
  }
  if (!found) {
    return false;
  }
  bestIndexOut = bestIndex;
  return true;
}

inline bool validDevicePassword(const char *password) {
  return validWifiPassword(password, false);
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
  REMOTE_ON,
  REMOTE_OFF,
  RINSE,
  STOP,
  STOP_HEARTBEAT,
  APPLY_CONFIG,
  RESET_WEIGHT_OFFSET,
  RESET_AUTO_TO_MANUAL_GUARD_SAMPLES,
  PRESET_OP,
  SAVE_NETWORK,
  FORGET_NETWORK,
  CHANGE_DEVICE_PASSWORD,
  RESET_DEVICE_PASSWORD,
  RESTART,
  RESET_NETWORK_AP,
  FACTORY_RESET,
  CLEAR_SHOT_LOG,
  CLEAR_RESET_HISTORY,
  SAVE_WEBHOOK,
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
  BLE_SCAN_INTENSITY,
  TASK_PROFILER_START,
  TASK_PROFILER_STOP,
  STATE_OVERRIDE_OFF,
  STATE_OVERRIDE_ON,
  MAINTENANCE_COMPLETE
};

inline const char *webCommandTypeName(WebCommandType type) {
  switch (type) {
    case WebCommandType::REMOTE_ON: return "remote web on";
    case WebCommandType::REMOTE_OFF: return "remote web off";
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
    case WebCommandType::CHANGE_DEVICE_PASSWORD: return "change device password";
    case WebCommandType::RESET_DEVICE_PASSWORD:
      return "restore device password";
    case WebCommandType::RESTART: return "restart";
    case WebCommandType::RESET_NETWORK_AP: return "recover network/AP";
    case WebCommandType::FACTORY_RESET: return "restore factory settings";
    case WebCommandType::CLEAR_SHOT_LOG: return "clear shot history";
    case WebCommandType::CLEAR_RESET_HISTORY: return "clear reset history";
    case WebCommandType::SAVE_WEBHOOK: return "save webhook";
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
    case WebCommandType::BLE_SCAN_INTENSITY: return "set BLE scan intensity";
    case WebCommandType::TASK_PROFILER_START: return "start task profiler";
    case WebCommandType::TASK_PROFILER_STOP: return "stop task profiler";
    case WebCommandType::STATE_OVERRIDE_OFF:
      return "override inferred idle";
    case WebCommandType::STATE_OVERRIDE_ON:
      return "override inferred brewing";
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
  // The 501-byte RTTTL text is staged in a fixed PSRAM mailbox instead of
  // inflating every four-deep FreeRTOS command queue element in internal RAM.
  bool bullseyeConfigSpecified = false;
  uint32_t bullseyeStageRequestId = 0;
  uint32_t webhookStageRequestId = 0;
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
  // Admin "Wi-Fi sleep". USB/BLE leave wifiSleepSpecified false so
  // SET_WIFI does not clobber the persisted flag.
  bool wifiSleep = false;
  bool wifiSleepSpecified = false;
  bool bleScanIntensitySpecified = false;
  uint8_t bleScanIntensity = 0;
  // USB SET_WIFI only. Web UI / BLE Companion keep the HTTP confirm window.
  bool commitConfirmed = false;
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
  uint32_t minBbwBrewTimeRemainingMs = 0;
  bool noScaleShotGuardEnabled = false;
  bool noScaleShotGuardArmed = false;
  char scaleProtocol[20] = "none";
  uint8_t rating = 0;
  // Uses existing alignment padding before shotLogId; keeps blob size stable.
  uint8_t noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  uint32_t shotLogId = 0;
};

struct ControlStatusSnapshot {
  StopperState state = StopperState::REQUIRES_OFF;
  bool activeCycle = false;
  bool relayClosed = false;
  bool machineRunning = false;
  bool reedOn = false;
  bool physicalActivatorOn = false;
  bool rawActivatorOn = false;
  bool virtualHoldOn = false;
  bool remoteControlEnabled = REMOTE_MACHINE_CONTROL_ENABLED;
  ControlSource source = ControlSource::NONE;
  uint32_t cycleId = 0;
  uint32_t bootId = 0;
  bool maintenanceLeaseActive = false;
  uint32_t maintenanceLeaseId = 0;
  uint32_t maintenanceStartedAtMs = 0;
  uint32_t circuitElapsedMs = 0;
  RelaySafetyState safetyState = RelaySafetyState::BOOT_SAFE;
  RelaySafetyFault safetyFault = RelaySafetyFault::NONE;
  uint32_t safetyGeneration = 0;
  bool safetyTimersReady = false;
  bool taskWatchdogReady = false;
  bool externalSafetyPresent = false;
  bool circuitFeedbackClosed = false;
  uint32_t resetReasonCode = 0;
  uint32_t unsafeResetCount = 0;
  bool resetRecoveryRequired = false;
  bool bootLoopDetected = false;
  uint8_t resetHistoryCount = 0;
  ResetHistoryEntry resetHistory[RESET_HISTORY_CAPACITY] = {};
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
  uint32_t scaleWeightUpdateIntervalMs = 0;
  uint32_t scaleRejectedPackets = 0;
  uint32_t scaleReconnects = 0;
  uint32_t scaleRecoveredStaleCount = 0;
  uint32_t scaleRecoveredStaleMs = 0;
  uint8_t scaleLastDisconnectReason = 0;
  bool scaleRssiValid = false;
  int8_t scaleRssi = 0;
  uint32_t uptimeMs = 0;
  // Recent loop gap: max over the last completed ~5 s health window (and any
  // larger gap already seen in the in-progress window). Lifetime max is
  // loopMaxGapMs and never decreases until reboot.
  uint32_t loopIntervalGapMs = 0;
  uint32_t loopMaxGapMs = 0;
  uint32_t loopStackMinWords = 0;
  uint32_t scaleStackMinWords = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t minimumFreeHeapBytes = 0;
  uint32_t largestFreeHeapBlockBytes = 0;
  uint32_t psramSizeBytes = 0;
  uint32_t psramFreeBytes = 0;
  uint32_t psramLargestFreeBlockBytes = 0;
  uint32_t bleHostAllocPsramCount = 0;
  uint32_t bleHostAllocFallbackCount = 0;
  uint32_t bleHostHciRxDropped = 0;
  uint32_t bleHostHciTxDropped = 0;
  bool workBufExternal = false;
  bool jsonArenaExternal = false;
  uint32_t allocExternalFallbackCount = 0;
  uint32_t scaleEventsDropped = 0;
  RuntimeConfig config = {};
  LastCycleSummary lastCycle = {};
  PersistedLastShot lastShot = {};
  uint8_t shotCurveCount = 0;
  uint8_t shotCurveIntervalS = 2;
  uint16_t shotCurveFirstDropDs = UINT16_MAX;
  int16_t shotCurveFirstDropCg = INT16_MIN;
  uint16_t shotCurveExtendedDs = UINT16_MAX;
  int16_t shotCurveExtendedCg = INT16_MIN;
  uint16_t shotCurveAtmDs = UINT16_MAX;
  int16_t shotCurveAtmCg = INT16_MIN;
  uint16_t shotCurveAtmClearedDs = UINT16_MAX;
  uint16_t shotCurveEndedDs = UINT16_MAX;
  int16_t shotCurveEndedCg = INT16_MIN;
  int16_t shotCurveWeightCg[31] = {};
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
  uint32_t cycleMinBbwBrewTimeRemainingMs = 0;
  bool cycleAutoToManualGuardArmed = false;
  bool cycleAutoToManualGuardEnforced = false;
  uint32_t cycleAutoToManualGuardRemainingMs = 0;
  bool cycleAccidentalTouchHolding = false;
  // New fields: consider debug export (ShotStopperDebugExport.h).
  uint8_t cycleAccidentalTouchPhase = 0;
  uint8_t cycleAccidentalTouchClass = 0;
  uint8_t cycleAccidentalTouchPendingCount = 0;
  bool cycleCupRemovedPending = false;
  bool cycleBbwProtectionEnabled = false;
  bool cycleBbwProtectionEnded = false;
  uint32_t autoToManualGuardTrendMs = DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  char scaleProtocol[20] = "none";
  char preferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char preferredScaleName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  uint32_t scaleMacCachePauseRemainingMs = 0;
  bool noScaleShotGuardEnabled = true;
  bool noScaleShotGuardArmed = true;
  bool noScaleShotGuardHold = false;
  bool noScaleShotGuardScaleWasAvailable = false;
  uint32_t noScaleShotGuardCooldownRemainingMs = 0;
  bool cupStartGuardHold = false;
  MachineRunState machineRunState = MachineRunState::CONFIRMED_OFF;
  bool machineStartAckPending = false;
  bool machineStopAckPending = false;
  bool machineOrphanRun = false;
  CupPresenceState cupPresenceState = CupPresenceState::ABSENT;
  bool cupPresent = false;
  bool configPersistPending = false;
  bool configPersistFailed = false;
  bool bootComplete = false;
  bool bootDegraded = false;
  bool scaleWorkerReady = false;
  bool usbConsoleIo4Closed = false;
  UsbSerialEnableSource usbSerialEnableSource = UsbSerialEnableSource::OFF;
  uint32_t bleCompanionResultDropped = 0;
  bool bleCompanionEnabled = false;
  bool bleCompanionActive = false;
  bool bleCompanionRestartRequired = false;
  bool bleCompanionStackReady = false;
  bool bleCompanionAdvertising = false;
  bool bleCompanionConnected = false;
  uint8_t bleCompanionProtocolVersion = 2;
  uint32_t bleCompanionAcceptedWrites = 0;
  uint32_t bleCompanionRejectedWrites = 0;
  uint8_t bleCompanionLastReject = 0;
  uint8_t bleCompanionScanIntensity = 0;
};

// Published copy lives in BSS, not on the 8 KiB loop stack.
static_assert(sizeof(ControlStatusSnapshot) <= 4096,
              "ControlStatusSnapshot grew past the loop-stack status budget");

// Gate checks (Ready / machine working / paddle / lease) do not need the ~876 B
// snapshot. Network and httpd copy this onto the stack; the full snapshot stays
// in DRAM BSS (control path) or NetworkWorkBuf (PSRAM, HTTP).
struct ControlGateSnapshot {
  StopperState state = StopperState::REQUIRES_OFF;
  bool activeCycle = false;
  bool relayClosed = false;
  bool machineRunning = false;
  bool physicalActivatorOn = false;
  bool maintenanceLeaseActive = false;
  uint32_t maintenanceLeaseId = 0;
  ControlSource source = ControlSource::NONE;
};

static_assert(sizeof(ControlGateSnapshot) <= 32,
              "ControlGateSnapshot must stay a stack-safe gate copy");

inline ControlGateSnapshot controlGateOf(const ControlStatusSnapshot &status) {
  ControlGateSnapshot gate;
  gate.state = status.state;
  gate.activeCycle = status.activeCycle;
  gate.relayClosed = status.relayClosed;
  gate.machineRunning = status.machineRunning;
  gate.physicalActivatorOn = status.physicalActivatorOn;
  gate.maintenanceLeaseActive = status.maintenanceLeaseActive;
  gate.maintenanceLeaseId = status.maintenanceLeaseId;
  gate.source = status.source;
  return gate;
}

inline bool controlAllowsConfiguration(const ControlGateSnapshot &status) {
  // Relay safety is enforced independently when the brew circuit is armed. A
  // safety lockout must keep the relay open, but it must not lock the WebUI or
  // prevent recovery-time configuration and diagnostics.
  // Config lock is "machine working", not "K1 energized". For paddle those
  // coincide (circuit closed == group brewing).
  return status.state == StopperState::READY && !status.activeCycle &&
         !status.machineRunning && !status.physicalActivatorOn &&
         !status.maintenanceLeaseActive;
}

inline bool controlAllowsConfiguration(const ControlStatusSnapshot &status) {
  return controlAllowsConfiguration(controlGateOf(status));
}

// Shot-log / last-shot NVS. WebUI override may mutate config during a pour;
// it must not disable flash cache while the machine is working.
inline bool controlAllowsHistoryMutation(const ControlGateSnapshot &status) {
  return !status.activeCycle && !status.machineRunning;
}

inline bool controlAllowsHistoryMutation(const ControlStatusSnapshot &status) {
  return controlAllowsHistoryMutation(controlGateOf(status));
}

enum class DebugCategory : uint8_t {
  ACTIVATOR,
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

enum class CircuitArmFailReason : int32_t {
  INVALID_LIMIT = 1,
  SAFETY_LOCKOUT = 2,
  SUPERVISOR_UNAVAILABLE = 3,
  FEEDBACK_STUCK_CLOSED = 4,
  TIMER_ARM_FAILED = 5,
  ARM_CANCELED = 6
};

enum class DebugCode : uint8_t {
  ACTIVATOR_ON,
  ACTIVATOR_OFF,
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
  WEB_REMOTE_ON,
  WEB_REMOTE_OFF,
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
  ACCIDENTAL_TOUCH_HOLD,
  ACCIDENTAL_TOUCH_RELEASE,
  ACCIDENTAL_TOUCH_HANDOFF,
  ACCIDENTAL_TOUCH_SUSTAINED,
  SHOT_LOG_PERSIST_FAILED,
  RUNTIME_PERSIST_FAILED,
  BOOT_BANNER,
  BOOT_RESET_REASON,
  BOOT_SUBSYSTEM,
  BOOT_RUNTIME_CONFIG,
  BOOT_READY,
  SAFETY_LOCKOUT_ACTIVE,
  CIRCUIT_ARM_FAILED,
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
  DEVICE_PASSWORD_RESET,
  HEALTH_HEAP_LOW,
  HEALTH_HEAP_RESTART,
  HEALTH_STACK_LOW,
  HEALTH_LOOP_GAP,
  OTA_UPLOAD_STARTED,
  OTA_IMAGE_STAGED,
  OTA_UPLOAD_REJECTED,
  OTA_FLASH_COMMITTED,
  OTA_IMAGE_CONFIRMED,
  OTA_ROLLBACK_ARMED,
  OTA_ROLLBACK_FAILED,
  RELAY_GPIO_DESYNC,
  SCALE_SCAN_STARTED,
  SCALE_SCAN_WAITING,
  SCALE_GATT_CONNECTING,
  SCALE_CONNECT_ATTEMPT_FAILED,
  SCALE_CONNECT_FAILED
};

// argument1 for SCALE_SCAN_STARTED / SCALE_GATT_CONNECTING.
constexpr int32_t SCALE_SCAN_TARGET_ANY = 0;
constexpr int32_t SCALE_SCAN_TARGET_PREFERRED = 1;
// argument1 for SCALE_SCAN_WAITING.
constexpr int32_t SCALE_SCAN_WAIT_NO_ADVERT = 0;
constexpr int32_t SCALE_SCAN_WAIT_OTHER_SCALE = 1;

// Health telemetry thresholds. Never close or open the machine circuit from
// these samples. Sustained internal-heap pressure may request a safe restart
// only from Ready with the circuit open (see HEALTH_HEAP_LOW_RESTART_MS).
// Clear values are higher than alert values for hysteresis / rising-edge only.
constexpr uint32_t HEALTH_HEAP_FREE_ALERT_BYTES = 49152;   // 48 KiB
constexpr uint32_t HEALTH_HEAP_FREE_CLEAR_BYTES = 65536;   // 64 KiB
constexpr uint32_t HEALTH_HEAP_LARGEST_ALERT_BYTES = 16384; // 16 KiB
constexpr uint32_t HEALTH_HEAP_LARGEST_CLEAR_BYTES = 24576; // 24 KiB
constexpr uint32_t HEALTH_HEAP_LOW_RESTART_MS = 5UL * 60UL * 1000UL;
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
    case DebugCode::CIRCUIT_ARM_FAILED:
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
    case DebugCode::HEALTH_HEAP_RESTART:
    case DebugCode::HEALTH_STACK_LOW:
    case DebugCode::HEALTH_LOOP_GAP:
    case DebugCode::OTA_UPLOAD_REJECTED:
    case DebugCode::RELAY_GPIO_DESYNC:
    case DebugCode::SCALE_CONNECT_ATTEMPT_FAILED:
    case DebugCode::SCALE_CONNECT_FAILED:
      return LogLevel::WARNING;
    case DebugCode::OTA_ROLLBACK_ARMED:
    case DebugCode::OTA_ROLLBACK_FAILED:
      return LogLevel::CRITICAL;
    case DebugCode::SCALE_CONNECTING:
    case DebugCode::SCALE_SCAN_WAITING:
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
    case DebugCategory::ACTIVATOR: return "activator";
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

inline const char *circuitArmFailReasonName(CircuitArmFailReason reason) {
  switch (reason) {
    case CircuitArmFailReason::INVALID_LIMIT: return "invalid safety limit";
    case CircuitArmFailReason::SAFETY_LOCKOUT: return "safety lockout is active";
    case CircuitArmFailReason::SUPERVISOR_UNAVAILABLE:
      return "safety supervisor unavailable";
    case CircuitArmFailReason::FEEDBACK_STUCK_CLOSED:
      return "feedback is already closed";
    case CircuitArmFailReason::TIMER_ARM_FAILED:
      return "failed to arm safety deadline";
    case CircuitArmFailReason::ARM_CANCELED:
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
    case DebugCode::ACTIVATOR_ON: return "activator on";
    case DebugCode::ACTIVATOR_OFF: return "activator off";
    case DebugCode::RELAY_CLOSED: return "machine circuit closed";
    case DebugCode::RELAY_OPENED: return "machine circuit opened";
    case DebugCode::HARD_LIMIT: return "hard limit reached";
    case DebugCode::OPERATIONAL_LIMIT:
      return "configured wall limit reached";
    case DebugCode::STATE_TRANSITION: return "state transition";
    case DebugCode::SCALE_CONNECTING: return "scale connecting";
    case DebugCode::SCALE_SCAN_STARTED: return "scale scan started";
    case DebugCode::SCALE_SCAN_WAITING: return "scale scan waiting";
    case DebugCode::SCALE_GATT_CONNECTING: return "scale GATT connecting";
    case DebugCode::SCALE_CONNECT_ATTEMPT_FAILED:
      return "scale GAP connect attempt failed";
    case DebugCode::SCALE_CONNECT_FAILED: return "scale connect failed";
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
      return "auto-to-manual guard opened machine circuit";
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
    case DebugCode::WEB_REMOTE_ON: return "remote web on";
    case DebugCode::WEB_REMOTE_OFF: return "remote web off";
    case DebugCode::WEB_RINSE: return "rinse web started";
    case DebugCode::WEB_COMMAND_ACCEPTED: return "web command accepted";
    case DebugCode::WEB_COMMAND_REJECTED: return "web command rejected";
    case DebugCode::WEB_STOP: return "web safe stop";
    case DebugCode::RESTART_REQUESTED: return "restart requested";
    case DebugCode::NETWORK_RESET: return "network settings reset";
    case DebugCode::DEVICE_PASSWORD_RESET: return "Device password restored";
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
      return "fast extraction guard stopped at min BBW brew time";
    case DebugCode::SLOW_EXTRACTION_ENTERED:
      return "slow extraction guard extended shot";
    case DebugCode::SLOW_EXTRACTION_STOP_MAX_TIME:
      return "slow extraction guard stopped at max BBW brew time";
    case DebugCode::SLOW_EXTRACTION_STOP_MIN_WEIGHT:
      return "slow extraction guard stopped at min weight";
    case DebugCode::ACCIDENTAL_TOUCH_HOLD:
      return "accidental touch holding weight cut";
    case DebugCode::ACCIDENTAL_TOUCH_RELEASE:
      return "accidental touch released";
    case DebugCode::ACCIDENTAL_TOUCH_HANDOFF:
      return "accidental touch trend handoff";
    case DebugCode::ACCIDENTAL_TOUCH_SUSTAINED:
      return "accidental touch sustained weight accepted";
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
    case DebugCode::CIRCUIT_ARM_FAILED: return "cannot close the machine circuit";
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
    case DebugCode::HEALTH_HEAP_RESTART: return "health heap restart";
    case DebugCode::HEALTH_STACK_LOW: return "health stack low";
    case DebugCode::HEALTH_LOOP_GAP: return "health loop gap high";
    case DebugCode::OTA_UPLOAD_STARTED: return "OTA upload started";
    case DebugCode::OTA_IMAGE_STAGED: return "OTA image staged";
    case DebugCode::OTA_UPLOAD_REJECTED: return "OTA upload rejected";
    case DebugCode::OTA_FLASH_COMMITTED: return "OTA boot image switched";
    case DebugCode::OTA_IMAGE_CONFIRMED: return "OTA image confirmed";
    case DebugCode::OTA_ROLLBACK_ARMED: return "OTA rollback armed";
    case DebugCode::OTA_ROLLBACK_FAILED: return "OTA rollback failed";
    case DebugCode::RELAY_GPIO_DESYNC:
      return "relay GPIO desync";
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

inline const char *scaleDisconnectReasonDebugName(int32_t reason) {
  switch (reason) {
    case 0: return "none";
    case 1: return "user request";
    case 2: return "scan start failed";
    case 3: return "scan timeout";
    case 4: return "connect failed";
    case 5: return "discovery failed";
    case 6: return "unsupported scale";
    case 7: return "subscribe failed";
    case 8: return "initialization write failed";
    case 9: return "remote disconnected";
    case 10: return "first packet timeout";
    case 11: return "packet timeout";
    case 12: return "invalid packet stream";
    case 13: return "command write failed";
    case 14: return "supervision timeout";
    case 15: return "connection failed to be established";
  }
  return "unknown";
}

inline const char *scaleConnectStepDebugName(int32_t step) {
  switch (step) {
    case 1: return "settle";
    case 2: return "connect";
    case 3: return "discover";
    case 4: return "configure";
    case 5: return "subscribe";
    case 6: return "init writes";
    default: return "idle";
  }
}

inline const char *endReasonDebugName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "none";
    case EndReason::ACTIVATOR: return "activator";
    case EndReason::SCALE_THRESHOLD: return "scale threshold";
    case EndReason::WEIGHT_ANOMALY: return "weight anomaly";
    case EndReason::GLOBAL_LIMIT: return "global machine circuit limit";
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
    case EndReason::UNCONFIRMED_START: return "unconfirmed start";
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
    case DebugCode::CIRCUIT_ARM_FAILED:
      snprintf(message, capacity, "cannot close the machine circuit: %s",
               circuitArmFailReasonName(
                   static_cast<CircuitArmFailReason>(event.argument1)));
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
    case DebugCode::HEALTH_HEAP_RESTART:
      snprintf(message, capacity,
               "health heap restart free=%ld largest=%ld",
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
    case DebugCode::SCALE_SCAN_STARTED:
      snprintf(message, capacity, "scale scan started (%s, intensity=%s)",
               event.argument1 == SCALE_SCAN_TARGET_PREFERRED ? "preferred"
                                                              : "any",
               bleScanIntensityName(clampBleScanIntensity(
                   static_cast<uint8_t>(event.argument2))));
      return true;
    case DebugCode::SCALE_SCAN_WAITING:
      snprintf(message, capacity, "scale scan waiting: %s",
               event.argument1 == SCALE_SCAN_WAIT_OTHER_SCALE
                   ? "other scale seen"
                   : "no advertisement");
      return true;
    case DebugCode::SCALE_GATT_CONNECTING:
      snprintf(message, capacity, "scale GATT connecting (%s)",
               event.argument1 == SCALE_SCAN_TARGET_PREFERRED ? "preferred"
                                                              : "any");
      return true;
    case DebugCode::SCALE_CONNECT_ATTEMPT_FAILED:
      snprintf(message, capacity,
               "scale GAP connect attempt %ld failed (step=%s)",
               static_cast<long>(event.argument1),
               scaleConnectStepDebugName(event.argument2));
      return true;
    case DebugCode::SCALE_CONNECT_FAILED:
      snprintf(message, capacity, "scale connect failed: %s (step=%s)",
               scaleDisconnectReasonDebugName(event.argument1),
               scaleConnectStepDebugName(event.argument2));
      return true;
    case DebugCode::SCALE_DISCONNECTED:
      if (event.argument1 <= 0) {
        copyCString(message, capacity, debugCodeName(event.code));
      } else {
        snprintf(message, capacity, "scale disconnected: %s",
                 scaleDisconnectReasonDebugName(event.argument1));
      }
      return true;
    case DebugCode::SCALE_PACKET_GAP:
      snprintf(message, capacity, "scale packet gap seq=%ld dt=%ld ms",
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    case DebugCode::OTA_UPLOAD_STARTED:
      snprintf(message, capacity, "OTA upload started size=%ld KiB",
               static_cast<long>(event.argument1));
      return true;
    case DebugCode::OTA_IMAGE_STAGED:
      snprintf(message, capacity, "OTA image staged size=%ld KiB packed=0x%08lx",
               static_cast<long>(event.argument1),
               static_cast<unsigned long>(event.argument2));
      return true;
    case DebugCode::OTA_UPLOAD_REJECTED:
      snprintf(message, capacity, "OTA upload rejected reason=%ld at %ld KiB",
               static_cast<long>(event.argument1),
               static_cast<long>(event.argument2));
      return true;
    case DebugCode::OTA_FLASH_COMMITTED:
      snprintf(message, capacity,
               "OTA boot image switched packed=0x%08lx; restarting",
               static_cast<unsigned long>(event.argument1));
      return true;
    case DebugCode::OTA_ROLLBACK_ARMED:
      snprintf(message, capacity,
               "OTA rollback armed after %ld s without a Web UI",
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
