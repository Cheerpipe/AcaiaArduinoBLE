#pragma once

// Current shot-log schema (v7) and domain helpers. No Preferences / NVS.

#include "ShotStopperDomain.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t SHOT_LOG_MAGIC = 0x534C4F47U;  // "SLOG"
constexpr uint16_t SHOT_LOG_SCHEMA_VERSION = 7;
constexpr size_t SHOT_LOG_CAPACITY = 120;
constexpr uint32_t MIN_SHOT_LOG_DURATION_MS = 10000;
// INT16_MIN leaves the full positive int16 centigram range usable.
constexpr int16_t SHOT_LOG_WEIGHT_MISSING = INT16_MIN;
constexpr int16_t SHOT_LOG_WEIGHT_MISSING_LEGACY = INT16_MAX;
constexpr uint16_t SHOT_LOG_METRIC_MISSING = UINT16_MAX;

inline bool shotLogWeightIsMissing(int16_t centigrams) {
  return centigrams == SHOT_LOG_WEIGHT_MISSING ||
         centigrams == SHOT_LOG_WEIGHT_MISSING_LEGACY;
}

inline int16_t normalizeLegacyShotLogWeight(int16_t centigrams) {
  return centigrams == SHOT_LOG_WEIGHT_MISSING_LEGACY ? SHOT_LOG_WEIGHT_MISSING
                                                     : centigrams;
}

enum class ShotLogType : uint8_t {
  AUTO = 0,
  TIMER_ONLY = 1,
  MANUAL = 2
};

enum class ShotLogCut : uint8_t {
  AUTO = 0,
  MANUAL = 1,
  LIMIT = 2
};

enum class ShotLogStopDetail : uint8_t {
  NORMAL_TARGET = 0,
  PREDICTION = 1,  // legacy writes only; new shots never store this
  EXTENDED_MAX_WEIGHT = 2,
  EXTENDED_MIN_TIME = 3,
  AUTO_TO_MANUAL = 4,
  SLOW_MAX_TIME = 5,
  SLOW_MIN_WEIGHT = 6,
  CUP_REMOVED = 7,
  OTHER = 255
};

constexpr uint8_t SHOT_LOG_FAST_GUARD_BIT = 0x01;
constexpr uint8_t SHOT_LOG_SLOW_GUARD_BIT = 0x02;
constexpr uint8_t SHOT_LOG_FAST_EXTENDED_BIT = 0x01;
constexpr uint8_t SHOT_LOG_SLOW_EXTENDED_BIT = 0x02;

inline uint8_t shotLogPackGuardFlags(bool fastEnabled, bool slowEnabled) {
  return static_cast<uint8_t>((fastEnabled ? SHOT_LOG_FAST_GUARD_BIT : 0) |
                              (slowEnabled ? SHOT_LOG_SLOW_GUARD_BIT : 0));
}

inline uint8_t shotLogPackExtendedFlags(bool fastExtended, bool slowExtended) {
  return static_cast<uint8_t>((fastExtended ? SHOT_LOG_FAST_EXTENDED_BIT : 0) |
                              (slowExtended ? SHOT_LOG_SLOW_EXTENDED_BIT : 0));
}

inline bool shotLogFastGuardEnabled(uint8_t flags) {
  return (flags & SHOT_LOG_FAST_GUARD_BIT) != 0;
}

inline bool shotLogSlowGuardEnabled(uint8_t flags) {
  return (flags & SHOT_LOG_SLOW_GUARD_BIT) != 0;
}

inline bool shotLogFastExtended(uint8_t flags) {
  return (flags & SHOT_LOG_FAST_EXTENDED_BIT) != 0;
}

inline bool shotLogSlowExtended(uint8_t flags) {
  return (flags & SHOT_LOG_SLOW_EXTENDED_BIT) != 0;
}

inline const char *shotLogStopDetailName(ShotLogStopDetail detail) {
  switch (detail) {
    case ShotLogStopDetail::NORMAL_TARGET: return "normal_target";
    case ShotLogStopDetail::PREDICTION: return "prediction";
    case ShotLogStopDetail::EXTENDED_MAX_WEIGHT: return "extended_max_weight";
    case ShotLogStopDetail::EXTENDED_MIN_TIME: return "extended_min_time";
    case ShotLogStopDetail::AUTO_TO_MANUAL: return "auto_to_manual";
    case ShotLogStopDetail::SLOW_MAX_TIME: return "slow_max_time";
    case ShotLogStopDetail::SLOW_MIN_WEIGHT: return "slow_min_weight";
    case ShotLogStopDetail::CUP_REMOVED: return "cup_removed";
    case ShotLogStopDetail::OTHER: return "other";
  }
  return "unknown";
}

inline const char *actualWeightSourceName(ActualWeightSource source) {
  switch (source) {
    case ActualWeightSource::NONE: return "none";
    case ActualWeightSource::POST_DRIP: return "post_drip";
    case ActualWeightSource::LAST_KNOWN: return "last_known";
  }
  return "unknown";
}

inline ShotLogStopDetail shotLogStopDetailFromEndReason(
    EndReason reason, bool extractionGuardEnabled, bool extractionExtended) {
  switch (reason) {
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
      return ShotLogStopDetail::EXTENDED_MAX_WEIGHT;
    case EndReason::FAST_EXTRACTION_MIN_TIME:
      return ShotLogStopDetail::EXTENDED_MIN_TIME;
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
      return ShotLogStopDetail::SLOW_MAX_TIME;
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
      return ShotLogStopDetail::SLOW_MIN_WEIGHT;
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return ShotLogStopDetail::AUTO_TO_MANUAL;
    case EndReason::CUP_REMOVED:
      return ShotLogStopDetail::CUP_REMOVED;
    case EndReason::SCALE_THRESHOLD:
    case EndReason::WEIGHT_ANOMALY:
      return extractionExtended && extractionGuardEnabled
                 ? ShotLogStopDetail::OTHER
                 : ShotLogStopDetail::NORMAL_TARGET;
    default:
      return ShotLogStopDetail::OTHER;
  }
}

inline const char *shotLogTypeName(ShotLogType type) {
  switch (type) {
    case ShotLogType::AUTO: return "auto";
    case ShotLogType::TIMER_ONLY: return "timer_only";
    case ShotLogType::MANUAL: return "manual";
  }
  return "unknown";
}

inline const char *shotLogCutName(ShotLogCut cut) {
  switch (cut) {
    case ShotLogCut::AUTO: return "auto";
    case ShotLogCut::MANUAL: return "manual";
    case ShotLogCut::LIMIT: return "limit";
  }
  return "unknown";
}

inline ShotLogCut shotLogCutFromEndReason(EndReason reason) {
  switch (reason) {
    case EndReason::SCALE_THRESHOLD:
    case EndReason::WEIGHT_ANOMALY:
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
    case EndReason::FAST_EXTRACTION_MIN_TIME:
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
    case EndReason::CUP_REMOVED:
      return ShotLogCut::AUTO;
    case EndReason::PADDLE:
    case EndReason::WEB_STOP:
    case EndReason::WEB_HEARTBEAT_TIMEOUT:
    case EndReason::PHYSICAL_OVERRIDE:
      return ShotLogCut::MANUAL;
    default:
      return ShotLogCut::LIMIT;
  }
}

inline bool shotLogEligible(EndReason reason, uint32_t durationMs) {
  if (reason == EndReason::SHORT_SHOT ||
      reason == EndReason::RINSE_COMPLETE) {
    return false;
  }
  return durationMs >= MIN_SHOT_LOG_DURATION_MS;
}

inline ShotLogType shotLogTypeFromCycle(StopperState finalState,
                                        bool startedWithScale, bool timerOnly,
                                        bool automaticBrew) {
  if (finalState == StopperState::MANUAL_NO_SCALE || !startedWithScale) {
    return ShotLogType::MANUAL;
  }
  if (timerOnly) {
    return ShotLogType::TIMER_ONLY;
  }
  if (automaticBrew) {
    return ShotLogType::AUTO;
  }
  return ShotLogType::MANUAL;
}

struct ShotLogRecord {
  uint32_t id;
  uint32_t bootId;
  uint32_t endedAtMs;
  uint32_t endedAtUnixSec;
  uint32_t endedAtLocalSec;
  int16_t timezoneOffsetMinutesAtCommit;
  uint16_t durationDs;
  uint8_t goalWeightG;
  uint8_t hasWallTime;
  int16_t actualWeightCg;
  int16_t errorCg;
  int16_t offsetUsedCg;
  uint16_t firstDropDs;
  uint16_t avgFlowCgS;
  uint8_t shotType;
  uint8_t cutType;
  uint8_t extractionGuardEnabled;
  uint8_t extractionExtended;
  uint8_t stopDetail;
  // Placed in the former v5 padding byte so sizeof stays 48 (no NVS growth).
  uint8_t actualWeightSource;
  int16_t maxRecoveryWeightCg;
  uint16_t minBrewTimeDs;
  uint16_t targetReachedEarlyDs;
};

static_assert(sizeof(ShotLogRecord) == 48,
              "ShotLogRecord must stay 48 bytes for NVS headroom");

struct ShotLogHeader {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t recordSize;
  uint32_t bootId;
  uint32_t nextRecordId;
  uint16_t count;
  uint16_t writeIndex;
  uint32_t checksum;
};

struct ShotLogStore {
  ShotLogHeader header;
  ShotLogRecord records[SHOT_LOG_CAPACITY];
};

inline uint32_t shotLogLocalSecFromUtc(uint32_t utcSec, int16_t offsetMinutes) {
  return utcSec + static_cast<int32_t>(offsetMinutes) * 60;
}

inline uint32_t shotLogChecksumBytes(const ShotLogHeader &header) {
  return crc32(reinterpret_cast<const uint8_t *>(&header),
               offsetof(ShotLogHeader, checksum));
}

// Schema 7+: header prefix + packed records[0..count). Legacy v2–v6 used
// header-only CRCs via shotLogChecksumBytes.
inline uint32_t shotLogChecksum(const ShotLogStore &store) {
  uint32_t crc = crc32Update(
      0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(&store.header),
      offsetof(ShotLogHeader, checksum));
  if (store.header.count > 0) {
    crc = crc32Update(crc, reinterpret_cast<const uint8_t *>(store.records),
                      static_cast<size_t>(store.header.count) *
                          sizeof(ShotLogRecord));
  }
  return ~crc;
}

inline void finalizeShotLogStore(ShotLogStore &store) {
  store.header.magic = SHOT_LOG_MAGIC;
  store.header.schemaVersion = SHOT_LOG_SCHEMA_VERSION;
  store.header.recordSize = sizeof(ShotLogRecord);
  store.header.checksum = 0;
  store.header.checksum = shotLogChecksum(store);
}

inline bool validShotLogStore(const ShotLogStore &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != SHOT_LOG_SCHEMA_VERSION ||
      store.header.recordSize != sizeof(ShotLogRecord) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksum(store)) {
    return false;
  }
  return true;
}

inline size_t shotLogPersistedBytes(const ShotLogStore &store) {
  return sizeof(ShotLogHeader) +
         static_cast<size_t>(store.header.count) * sizeof(ShotLogRecord);
}

inline bool shotLogBlobLengthMatches(const ShotLogStore &store, size_t length) {
  if (length == sizeof(ShotLogStore)) {
    return true;
  }
  return length == shotLogPersistedBytes(store);
}

inline void resetShotLogStore(ShotLogStore &store, uint32_t bootId) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = bootId == 0 ? 1U : bootId;
  store.header.nextRecordId = 1;
  finalizeShotLogStore(store);
}

inline int16_t shotLogWeightToCentigrams(float weightG) {
  if (!isfinite(weightG)) {
    return SHOT_LOG_WEIGHT_MISSING;
  }
  const int32_t centigrams = weightToCentigrams(weightG);
  // INT16_MIN is reserved as the missing sentinel.
  if (centigrams == INT32_MAX || centigrams <= INT16_MIN ||
      centigrams > INT16_MAX) {
    return SHOT_LOG_WEIGHT_MISSING;
  }
  return static_cast<int16_t>(centigrams);
}

}  // namespace shotstopper
