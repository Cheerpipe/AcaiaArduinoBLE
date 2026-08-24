#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ShotStopperMachineTypes.h"
#include "ShotStopperScaleTypes.h"

namespace shotstopper {

// =============================================================================
// LAYER: Brew types (stopper workflow + guard contracts)
// =============================================================================
// WHAT: StopperState, EndReason, GuardInputs, and brew timing/weight constants.
//       The stopper FSM decides rinse vs shot vs idle and *requests* circuit
//       start/stop; it does not drive GPIO or know paddle vs switch. The
//       activator translates GPIO into UserIntent before brew sees it.
//
// BOUNDARY: GuardInputs is a neutral snapshot the orchestrator fills once per
// loop. Brew/guards must not poll machine specializations, scale link, or cup
// presence themselves, and must not import paddle/momentary headers.

constexpr uint32_t DEFAULT_LAST_SHOT_COOLDOWN_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t MIN_LAST_SHOT_COOLDOWN_MS = 5UL * 60UL * 1000UL;
constexpr uint32_t MAX_LAST_SHOT_COOLDOWN_MS = 240UL * 60UL * 1000UL;
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
// Extra delay after the scale timer catches up to machine circuit time (whole seconds)
// before sending STOP_TIMER. 0 stops in that same instant.
constexpr uint32_t DEFAULT_SCALE_TIMER_STOP_EXTRA_DELAY_MS = 0;
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

// Snapshot the stopper builds once per loop. Brew/guards never poll machine,
// scale link, or cup presence themselves — and never see paddle/momentary
// internals (no PaddleMode, reed, pulse state, or MachineType branches).
struct GuardInputs {
  bool holdActive = false;
  bool scaleAvailable = false;
  bool scaleUsable = false;
  CupPresenceState cup = CupPresenceState::ABSENT;
  float currentWeightG = 0.0f;
};

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
  ACTIVATOR = 1,
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

inline bool brewWeightCutSettlesMachineOff(EndReason reason) {
  switch (reason) {
    case EndReason::SCALE_THRESHOLD:
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
    case EndReason::FAST_EXTRACTION_MIN_TIME:
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
      return true;
    default:
      return false;
  }
}

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

enum class BrewCommand : uint8_t {
  NONE = 0,
  BEGIN_BREW = 1,
  ENTER_RINSE = 2,
  REQUEST_MACHINE_START = 3,
  REQUEST_MACHINE_STOP = 4,
  REQUEST_RETARE = 5,
  SUSPEND_WEIGHT_CONTROL = 6,
  FINALIZE = 7
};

}  // namespace shotstopper
