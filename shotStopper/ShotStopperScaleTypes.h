#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

// =============================================================================
// LAYER: Scale types (link, weight stream, cup contracts)
// =============================================================================
// WHAT: Scale pairing/MAC modes, weight stream/control states, cup presence
//       enums, and shared scale constants used by sense + BLE link.
//
// BOUNDARY: Scale/cup vocabulary only. Machine paddle/momentary details do not
// belong here. Upper layers (brew, guards, stopper) consume these types; they
// must not push machine-specialization knowledge into this header.

inline void copyCString(char *destination, size_t capacity, const char *source) {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

constexpr size_t PREFERRED_SCALE_MAC_CAPACITY = 18;
constexpr size_t PREFERRED_SCALE_NAME_CAPACITY = 32;
constexpr size_t SCALE_HISTORY_CAPACITY = 8;
constexpr uint32_t SCALE_PAIRING_DISCOVERY_PAUSE_MS = 30000;
// PREFER: wait this long for the preferred MAC before connecting any other.
constexpr uint32_t SCALE_PREFER_FALLBACK_MS = 5000;

enum class ScaleMacCacheMode : uint8_t {
  // Numeric values: FIRST=0 and ONLY=1 match legacy OFF/FULL in NVS.
  FIRST = 0,  // Name scan; first compatible; do not lock preferred.
  ONLY = 1,   // Name scan + connect-filter; preferred MAC only.
  PREFER = 2, // Prefer preferred; after grace, fall back to any compatible.
};

// BLE-seen scales remembered for the preferred-scale dropdown (NVS + status).
struct ScaleHistoryEntry {
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  uint32_t lastSeenSeq = 0;
};

inline bool validScaleMacCacheMode(uint8_t mode) {
  return mode == static_cast<uint8_t>(ScaleMacCacheMode::FIRST) ||
         mode == static_cast<uint8_t>(ScaleMacCacheMode::ONLY) ||
         mode == static_cast<uint8_t>(ScaleMacCacheMode::PREFER);
}

inline bool scaleMacCacheModeRequiresPreferred(uint8_t mode) {
  return mode == static_cast<uint8_t>(ScaleMacCacheMode::ONLY) ||
         mode == static_cast<uint8_t>(ScaleMacCacheMode::PREFER);
}

inline const char *scaleMacCacheModeId(uint8_t mode) {
  switch (static_cast<ScaleMacCacheMode>(mode)) {
    case ScaleMacCacheMode::FIRST:
      return "first";
    case ScaleMacCacheMode::PREFER:
      return "prefer";
    case ScaleMacCacheMode::ONLY:
      return "only";
    default:
      return "first";
  }
}

inline bool parseScaleMacCacheMode(const char *text, uint8_t &mode) {
  if (text == nullptr) {
    return false;
  }
  // Legacy wire strings from the Always-use checkbox era.
  if (strcmp(text, "first") == 0 || strcmp(text, "disabled") == 0) {
    mode = static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
    return true;
  }
  if (strcmp(text, "prefer") == 0) {
    mode = static_cast<uint8_t>(ScaleMacCacheMode::PREFER);
    return true;
  }
  if (strcmp(text, "only") == 0 || strcmp(text, "full") == 0) {
    mode = static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
    return true;
  }
  return false;
}

constexpr uint32_t MAX_AUTOMATION_WEIGHT_AGE_MS = 1000;
constexpr float MIN_AUTOMATION_WEIGHT_G = -500.0f;
constexpr float MAX_AUTOMATION_WEIGHT_G = 1000.0f;
constexpr float MAX_AUTOMATION_WEIGHT_SLEW_G_PER_S = 100.0f;
constexpr float AUTOMATION_WEIGHT_SLEW_ALLOWANCE_G = 20.0f;
constexpr float POST_TARE_BASELINE_MAX_ABS_G = 50.0f;
constexpr uint32_t DEFAULT_POST_TARE_BASELINE_GRACE_MS = 2000;
constexpr uint32_t MIN_POST_TARE_BASELINE_GRACE_MS = 500;
constexpr uint32_t MAX_POST_TARE_BASELINE_GRACE_MS = 10000;
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
constexpr float FIRST_DROP_THRESHOLD_G = 0.3f;
constexpr float FIRST_DROP_FINGER_STEP_G = 2.0f;
constexpr float FIRST_DROP_BASELINE_SETTLE_G = 0.5f;
// Rewrite tare zero only for noise this close to 0; 0.25 g of coffee is not zero.
constexpr float FIRST_DROP_BASELINE_LOCK_G = 0.15f;
constexpr uint8_t FIRST_DROP_CONFIRMATION_SAMPLES = 2;
// Matches DEFAULT_MINIMUM_CUP_WEIGHT_G. Callers pass runtime min-cup.
constexpr float FIRST_DROP_DEFAULT_CUP_MIN_G = 10.0f;
constexpr float MAX_RECOVERY_WEIGHT_DROP_G = 2.0f;

// Post-shot sample is still the brew if it did not collapse vs last accepted
// (cup off / tare reads ~0). Drip may add weight or drop a little from settle.
inline bool plausibleSettledBrewWeight(float candidateG, float lastKnownG,
                                       bool lastKnownValid) {
  if (!isfinite(candidateG)) {
    return false;
  }
  if (!lastKnownValid || !isfinite(lastKnownG)) {
    return true;
  }
  return candidateG >= lastKnownG - MAX_RECOVERY_WEIGHT_DROP_G;
}
constexpr size_t WEIGHT_TREND_POINT_COUNT = 10;
constexpr float WEIGHT_TREND_MIN_LAST_SAMPLE_G = 10.0f;
constexpr float WEIGHT_TREND_MIN_HORIZON_S = 0.25f;
constexpr float ACCIDENTAL_TOUCH_RESIDUAL_G = 1.5f;
constexpr float ACCIDENTAL_TOUCH_STARTUP_MIN_DELTA_G = 2.0f;
constexpr float ACCIDENTAL_TOUCH_STARTUP_MAX_RATE_G_S = 8.0f;
constexpr float ACCIDENTAL_TOUCH_TREND_MIN_RATE_G_S = 3.0f;
constexpr float ACCIDENTAL_TOUCH_RATE_FACTOR = 3.0f;
constexpr float ACCIDENTAL_TOUCH_MIN_SLOPE_G_S = 0.2f;
constexpr float ACCIDENTAL_TOUCH_SUSTAINED_SPAN_G = 1.0f;
constexpr uint8_t ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES = 3;
constexpr size_t ACCIDENTAL_TOUCH_RATE_WINDOW = 4;

// Sliding window for brew-by-weight prediction. Keep this tiny and in
// internal RAM — do not allocate on heap or PSRAM on the weight path.
constexpr size_t MAX_SHOT_DATAPOINTS = 32;

enum class ScaleSignal : uint8_t {
  NONE = 0,
  FIRST_DROP = 1,
  STABLE_CUP = 2,
  THRESHOLD_CONFIRM = 3,
  PREDICTED_CUT_DUE = 4,
  CUP_LIKELY_REMOVED = 5,
  STREAM_STALE = 6,
  STREAM_FAULT = 7
};

enum class CupPresenceState : uint8_t {
  ABSENT = 0,
  PRESENT = 1
};

inline const char *cupPresenceStateName(CupPresenceState state) {
  switch (state) {
    case CupPresenceState::ABSENT: return "ABSENT";
    case CupPresenceState::PRESENT: return "PRESENT";
  }
  return "ABSENT";
}

enum class CupPresenceEvent : uint8_t {
  NONE = 0,
  PLACED = 1,
  REMOVED = 2
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

enum class AccidentalTouchPhase : uint8_t {
  STARTUP = 0,
  TREND = 1
};

enum class AccidentalTouchClass : uint8_t {
  OK = 0,
  TOUCH = 1,
  SUSTAINED = 2
};

enum class FirstFlowPhase : uint8_t {
  SEEKING = 0,
  TOUCH = 1
};

enum class FirstFlowClass : uint8_t {
  NONE = 0,
  CANDIDATE = 1,
  TOUCH = 2,
  FIRE = 3
};

struct FirstFlowState {
  FirstFlowPhase phase = FirstFlowPhase::SEEKING;
  uint8_t confirmations = 0;
  uint8_t residualConfirmations = 0;
  bool hasLastSample = false;
  float lastWeightG = 0.0f;
  uint32_t lastAtMs = 0;
  uint32_t lastPacketSequence = 0;
  float peakG = 0.0f;
  float postJumpG = 0.0f;
  uint32_t jumpAtMs = 0;
  uint32_t candidateMs = 0;
};

inline void resetFirstFlowState(FirstFlowState &state) {
  state = FirstFlowState{};
}

inline bool firstFlowPacketsConsecutive(const FirstFlowState &state,
                                        uint32_t receivedAtMs,
                                        uint32_t packetSequence) {
  return state.hasLastSample &&
         packetSequence == state.lastPacketSequence + 1U &&
         static_cast<int32_t>(receivedAtMs - state.lastAtMs) >= 0 &&
         static_cast<uint32_t>(receivedAtMs - state.lastAtMs) <=
             DIRECT_STOP_CONFIRMATION_WINDOW_MS;
}

inline void noteFirstFlowSample(FirstFlowState &state, float weight,
                                uint32_t receivedAtMs, uint32_t packetSequence) {
  state.hasLastSample = true;
  state.lastWeightG = weight;
  state.lastAtMs = receivedAtMs;
  state.lastPacketSequence = packetSequence;
}

inline FirstFlowClass enterFirstFlowTouch(FirstFlowState &state, float weight,
                                          uint32_t receivedAtMs) {
  state.phase = FirstFlowPhase::TOUCH;
  state.postJumpG = weight;
  state.peakG = weight;
  state.jumpAtMs = receivedAtMs;
  state.confirmations = 0;
  state.residualConfirmations = 0;
  state.candidateMs = receivedAtMs;
  return FirstFlowClass::TOUCH;
}

inline float firstFlowCupMinG(float cupMinG) {
  return (isfinite(cupMinG) && cupMinG > FIRST_DROP_THRESHOLD_G)
             ? cupMinG
             : FIRST_DROP_DEFAULT_CUP_MIN_G;
}

inline bool firstFlowIsCupMass(float delta, float cupMinG) {
  return delta >= firstFlowCupMinG(cupMinG);
}

inline bool firstFlowIsCoffeeLeftover(float delta, float cupMinG) {
  return delta >= FIRST_DROP_THRESHOLD_G &&
         delta < FIRST_DROP_FINGER_STEP_G && !firstFlowIsCupMass(delta, cupMinG);
}

// SEEKING: small coffee steps (< 2 g, below cup min) confirm first flow.
// TOUCH: a ≥2 g jump or mass ≥ cup min is a finger or cup, not first drop.
// Residual FIRE only if leftover is coffee-sized and the peak was below cup min.
// Through FIRE only while current weight is still below cup min.
inline FirstFlowClass stepFirstFlow(
    FirstFlowState &state, float weight, uint32_t receivedAtMs,
    uint32_t packetSequence, float baselineG,
    float cupMinG = FIRST_DROP_DEFAULT_CUP_MIN_G) {
  if (!isfinite(weight) || !isfinite(baselineG)) {
    return FirstFlowClass::NONE;
  }

  const float objectMinG = firstFlowCupMinG(cupMinG);
  const float delta = weight - baselineG;
  const float step =
      state.hasLastSample ? (weight - state.lastWeightG) : delta;
  const bool consecutive =
      firstFlowPacketsConsecutive(state, receivedAtMs, packetSequence);

  if (state.phase == FirstFlowPhase::SEEKING) {
    if (delta < FIRST_DROP_THRESHOLD_G) {
      state.confirmations = 0;
      state.residualConfirmations = 0;
      state.candidateMs = 0;
      noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
      return FirstFlowClass::NONE;
    }
    if (step >= FIRST_DROP_FINGER_STEP_G || delta >= objectMinG) {
      const FirstFlowClass classified =
          enterFirstFlowTouch(state, weight, receivedAtMs);
      noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
      return classified;
    }
    state.confirmations =
        consecutive && state.confirmations > 0
            ? static_cast<uint8_t>(state.confirmations + 1U)
            : 1U;
    if (state.confirmations == 1U) {
      state.candidateMs = receivedAtMs;
    }
    noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
    if (state.confirmations >= FIRST_DROP_CONFIRMATION_SAMPLES) {
      return FirstFlowClass::FIRE;
    }
    return FirstFlowClass::CANDIDATE;
  }

  state.peakG = fmaxf(state.peakG, weight);
  if (step >= FIRST_DROP_FINGER_STEP_G) {
    state.postJumpG = weight;
    state.peakG = fmaxf(state.peakG, weight);
    state.jumpAtMs = receivedAtMs;
    state.confirmations = 0;
    state.residualConfirmations = 0;
    state.candidateMs = receivedAtMs;
    noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
    return FirstFlowClass::TOUCH;
  }

  const bool released = weight <= state.peakG - FIRST_DROP_FINGER_STEP_G;
  if (released) {
    if (firstFlowIsCoffeeLeftover(delta, cupMinG) &&
        !firstFlowIsCupMass(state.peakG - baselineG, cupMinG)) {
      state.confirmations = 0;
      state.residualConfirmations =
          consecutive && state.residualConfirmations > 0
              ? static_cast<uint8_t>(state.residualConfirmations + 1U)
              : 1U;
      if (state.residualConfirmations == 1U) {
        state.candidateMs = receivedAtMs;
      }
      noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
      if (state.residualConfirmations >= FIRST_DROP_CONFIRMATION_SAMPLES) {
        return FirstFlowClass::FIRE;
      }
      return FirstFlowClass::TOUCH;
    }
    if (delta >= FIRST_DROP_THRESHOLD_G) {
      state.confirmations = 0;
      state.residualConfirmations = 0;
      noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
      return FirstFlowClass::TOUCH;
    }
    state.phase = FirstFlowPhase::SEEKING;
    state.confirmations = 0;
    state.residualConfirmations = 0;
    state.candidateMs = 0;
    noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
    return FirstFlowClass::NONE;
  }

  if (delta < objectMinG &&
      weight >= state.postJumpG + FIRST_DROP_FINGER_STEP_G &&
      step < FIRST_DROP_FINGER_STEP_G) {
    state.residualConfirmations = 0;
    state.confirmations =
        consecutive && state.confirmations > 0
            ? static_cast<uint8_t>(state.confirmations + 1U)
            : 1U;
    noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
    if (state.confirmations >= FIRST_DROP_CONFIRMATION_SAMPLES) {
      state.candidateMs = state.jumpAtMs;
      return FirstFlowClass::FIRE;
    }
    return FirstFlowClass::TOUCH;
  }

  state.confirmations = 0;
  state.residualConfirmations = 0;
  noteFirstFlowSample(state, weight, receivedAtMs, packetSequence);
  return FirstFlowClass::TOUCH;
}

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

struct WeightTrendFit {
  bool valid = false;
  float slope = 0.0f;
  float intercept = 0.0f;
};

inline WeightTrendFit fitWeightTrend(const float *timeS, const float *weightG,
                                     size_t datapoints) {
  WeightTrendFit fit;
  if (timeS == nullptr || weightG == nullptr ||
      datapoints < WEIGHT_TREND_POINT_COUNT) {
    return fit;
  }
  if (weightG[datapoints - 1] < WEIGHT_TREND_MIN_LAST_SAMPLE_G) {
    return fit;
  }

  float sumXY = 0.0f;
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumSquaredX = 0.0f;
  const size_t first = datapoints - WEIGHT_TREND_POINT_COUNT;
  for (size_t i = first; i < datapoints; ++i) {
    if (!isfinite(timeS[i]) || !isfinite(weightG[i])) {
      return fit;
    }
    sumXY += timeS[i] * weightG[i];
    sumX += timeS[i];
    sumY += weightG[i];
    sumSquaredX += timeS[i] * timeS[i];
  }

  const float n = static_cast<float>(WEIGHT_TREND_POINT_COUNT);
  const float denominator = n * sumSquaredX - sumX * sumX;
  if (fabsf(denominator) < 0.000001f) {
    return fit;
  }

  const float slope = (n * sumXY - sumX * sumY) / denominator;
  if (slope <= 0.0f || !isfinite(slope)) {
    return fit;
  }

  const float meanX = sumX / n;
  const float meanY = sumY / n;
  fit.slope = slope;
  fit.intercept = meanY - slope * meanX;
  fit.valid = isfinite(fit.intercept);
  return fit;
}

// Linear least-squares over the last WEIGHT_TREND_POINT_COUNT samples of
// (timeS, weightG); returns the time when weight reaches targetWeightG.
inline float predictedWeightStopTimeS(const float *timeS, const float *weightG,
                                      size_t datapoints, float targetWeightG,
                                      float fallbackEndS) {
  if (!isfinite(targetWeightG) || !isfinite(fallbackEndS)) {
    return fallbackEndS;
  }
  const WeightTrendFit fit = fitWeightTrend(timeS, weightG, datapoints);
  if (!fit.valid) {
    return fallbackEndS;
  }
  const float predicted = (targetWeightG - fit.intercept) / fit.slope;
  const float latestSampleS = timeS[datapoints - 1];
  if (!isfinite(predicted) ||
      predicted < latestSampleS + WEIGHT_TREND_MIN_HORIZON_S) {
    return fallbackEndS;
  }
  return predicted;
}

inline bool accidentalTouchTrendReady(const float *timeS, const float *weightG,
                                      size_t datapoints) {
  return fitWeightTrend(timeS, weightG, datapoints).valid;
}

inline float accidentalTouchMedianControlRate(const float *timeS,
                                              const float *weightG,
                                              size_t datapoints) {
  if (timeS == nullptr || weightG == nullptr || datapoints < 4) {
    return 0.0f;
  }
  float rates[ACCIDENTAL_TOUCH_RATE_WINDOW];
  size_t count = 0;
  const size_t first =
      datapoints > ACCIDENTAL_TOUCH_RATE_WINDOW
          ? datapoints - ACCIDENTAL_TOUCH_RATE_WINDOW
          : 1U;
  for (size_t i = first; i < datapoints && count < ACCIDENTAL_TOUCH_RATE_WINDOW;
       ++i) {
    if (!isfinite(timeS[i]) || !isfinite(timeS[i - 1]) ||
        !isfinite(weightG[i]) || !isfinite(weightG[i - 1])) {
      continue;
    }
    float dt = timeS[i] - timeS[i - 1];
    if (dt <= 0.0f) {
      dt = 0.001f;
    }
    rates[count++] = (weightG[i] - weightG[i - 1]) / dt;
  }
  if (count < 3) {
    return 0.0f;
  }
  for (size_t i = 1; i < count; ++i) {
    const float value = rates[i];
    size_t j = i;
    while (j > 0 && rates[j - 1] > value) {
      rates[j] = rates[j - 1];
      --j;
    }
    rates[j] = value;
  }
  if ((count % 2U) == 1U) {
    return rates[count / 2U];
  }
  return 0.5f * (rates[count / 2U - 1U] + rates[count / 2U]);
}

inline AccidentalTouchClass classifyAccidentalTouch(
    AccidentalTouchPhase phase, const float *timeS, const float *weightG,
    size_t datapoints, float weight, float timeSNow, bool hasAnchor,
    float lastAcceptedWeightG, float lastAcceptedTimeS,
    const float *pendingTouchG, uint8_t pendingTouchCount) {
  if (!hasAnchor || !isfinite(weight) || !isfinite(timeSNow) ||
      !isfinite(lastAcceptedWeightG) || !isfinite(lastAcceptedTimeS)) {
    return AccidentalTouchClass::OK;
  }

  float dt = timeSNow - lastAcceptedTimeS;
  if (dt <= 0.0f) {
    dt = 0.001f;
  }
  const float delta = weight - lastAcceptedWeightG;
  const float instantRate = delta / dt;
  const float medianRate =
      accidentalTouchMedianControlRate(timeS, weightG, datapoints);
  const float startupLimit = fmaxf(
      ACCIDENTAL_TOUCH_STARTUP_MAX_RATE_G_S,
      ACCIDENTAL_TOUCH_RATE_FACTOR * fmaxf(medianRate, 0.0f));
  const bool startupTouch =
      fabsf(delta) >= ACCIDENTAL_TOUCH_STARTUP_MIN_DELTA_G &&
      fabsf(instantRate) > startupLimit;

  bool anomalous = startupTouch;
  const WeightTrendFit fit = fitWeightTrend(timeS, weightG, datapoints);
  if (phase == AccidentalTouchPhase::TREND && fit.valid) {
    const float expected = fit.intercept + fit.slope * timeSNow;
    const float residual = weight - expected;
    const float trendLimit = fmaxf(
        ACCIDENTAL_TOUCH_TREND_MIN_RATE_G_S,
        ACCIDENTAL_TOUCH_RATE_FACTOR *
            fmaxf(fit.slope, ACCIDENTAL_TOUCH_MIN_SLOPE_G_S));
    anomalous =
        (residual > ACCIDENTAL_TOUCH_RESIDUAL_G && instantRate > trendLimit) ||
        (residual < -ACCIDENTAL_TOUCH_RESIDUAL_G && instantRate < -trendLimit);
  }

  if (!anomalous) {
    return AccidentalTouchClass::OK;
  }

  float lo = weight;
  float hi = weight;
  if (pendingTouchG != nullptr) {
    for (uint8_t i = 0; i < pendingTouchCount; ++i) {
      if (!isfinite(pendingTouchG[i])) {
        continue;
      }
      lo = fminf(lo, pendingTouchG[i]);
      hi = fmaxf(hi, pendingTouchG[i]);
    }
  }
  if (static_cast<uint8_t>(pendingTouchCount + 1U) >=
          ACCIDENTAL_TOUCH_SUSTAINED_SAMPLES &&
      (hi - lo) <= ACCIDENTAL_TOUCH_SUSTAINED_SPAN_G) {
    return AccidentalTouchClass::SUSTAINED;
  }
  return AccidentalTouchClass::TOUCH;
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
  copyCString(canonicalMac, sizeof(canonicalMac), mac);
  canonicalizePreferredScaleMac(canonicalMac, sizeof(canonicalMac));
  char safeName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name)) {
    copyCString(safeName, sizeof(safeName), name);
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
      copyCString(nameOut, nameOutCapacity, entries[i].name);
      return true;
    }
  }
  return false;
}

}  // namespace shotstopper
