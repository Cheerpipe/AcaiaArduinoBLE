#pragma once

#include "ShotStopperDomain.h"

#if defined(SHOT_STOPPER_HOST_TEST)
// State-machine host tests keep the shot log in memory only.
#elif !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <Preferences.h>
#endif
// Persistence host tests provide Preferences via persistence_host_stubs.h.

namespace shotstopper {

constexpr uint32_t SHOT_LOG_MAGIC = 0x534C4F47U;  // "SLOG"
constexpr uint16_t SHOT_LOG_SCHEMA_VERSION = 6;
constexpr size_t SHOT_LOG_CAPACITY = 120;
constexpr uint32_t MIN_SHOT_LOG_DURATION_MS = 10000;
constexpr float FIRST_DROP_THRESHOLD_G = 0.3f;
constexpr int16_t SHOT_LOG_WEIGHT_MISSING = INT16_MAX;
constexpr uint16_t SHOT_LOG_METRIC_MISSING = UINT16_MAX;

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
  PREDICTION = 1,
  EXTENDED_MAX_WEIGHT = 2,
  EXTENDED_MIN_TIME = 3,
  AUTO_TO_MANUAL = 4,
  OTHER = 255
};

inline const char *shotLogStopDetailName(ShotLogStopDetail detail) {
  switch (detail) {
    case ShotLogStopDetail::NORMAL_TARGET: return "normal_target";
    case ShotLogStopDetail::PREDICTION: return "prediction";
    case ShotLogStopDetail::EXTENDED_MAX_WEIGHT: return "extended_max_weight";
    case ShotLogStopDetail::EXTENDED_MIN_TIME: return "extended_min_time";
    case ShotLogStopDetail::AUTO_TO_MANUAL: return "auto_to_manual";
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
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return ShotLogStopDetail::AUTO_TO_MANUAL;
    case EndReason::SCALE_PREDICTION:
      return ShotLogStopDetail::PREDICTION;
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
    case EndReason::SCALE_PREDICTION:
    case EndReason::WEIGHT_ANOMALY:
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
    case EndReason::FAST_EXTRACTION_MIN_TIME:
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

inline ShotLogType shotLogTypeFromCycle(StopperState finalState, bool startedWithScale,
                                        bool timerOnly, bool confirmedBrew) {
  if (finalState == StopperState::MANUAL_NO_SCALE || !startedWithScale) {
    return ShotLogType::MANUAL;
  }
  if (timerOnly) {
    return ShotLogType::TIMER_ONLY;
  }
  if (confirmedBrew) {
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

struct ShotLogRecordV5 {
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
  int16_t maxRecoveryWeightCg;
  uint16_t minBrewTimeDs;
  uint16_t targetReachedEarlyDs;
};

static_assert(sizeof(ShotLogRecord) == 48,
              "ShotLogRecord must stay 48 bytes for NVS headroom");
static_assert(sizeof(ShotLogRecord) == sizeof(ShotLogRecordV5),
              "v6 record size must match v5 to avoid enlarging the blob");

struct ShotLogRecordV4 {
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
};

struct ShotLogRecordV3 {
  uint32_t id;
  uint32_t bootId;
  uint32_t endedAtMs;
  uint32_t endedAtUnixSec;
  uint16_t durationDs;
  uint8_t goalWeightG;
  int16_t actualWeightCg;
  int16_t errorCg;
  int16_t offsetUsedCg;
  uint16_t firstDropDs;
  uint16_t avgFlowCgS;
  uint8_t shotType;
  uint8_t cutType;
};

struct ShotLogRecordV2 {
  uint32_t id;
  uint32_t bootId;
  uint32_t endedAtMs;
  uint16_t durationDs;
  uint8_t goalWeightG;
  int16_t actualWeightCg;
  int16_t errorCg;
  int16_t offsetUsedCg;
  uint16_t firstDropDs;
  uint16_t avgFlowCgS;
  uint8_t shotType;
  uint8_t cutType;
};

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

struct ShotLogStoreV5 {
  ShotLogHeader header;
  ShotLogRecordV5 records[SHOT_LOG_CAPACITY];
};

struct ShotLogStoreV4 {
  ShotLogHeader header;
  ShotLogRecordV4 records[SHOT_LOG_CAPACITY];
};

struct ShotLogStoreV3 {
  ShotLogHeader header;
  ShotLogRecordV3 records[SHOT_LOG_CAPACITY];
};

struct ShotLogStoreV2 {
  ShotLogHeader header;
  ShotLogRecordV2 records[SHOT_LOG_CAPACITY];
};

inline uint32_t shotLogLocalSecFromUtc(uint32_t utcSec, int16_t offsetMinutes) {
  return utcSec + static_cast<int32_t>(offsetMinutes) * 60;
}

inline uint32_t shotLogChecksumBytes(const ShotLogHeader &header) {
  return crc32(reinterpret_cast<const uint8_t *>(&header),
               offsetof(ShotLogHeader, checksum));
}

inline uint32_t shotLogChecksum(const ShotLogStore &store) {
  return shotLogChecksumBytes(store.header);
}

inline uint32_t shotLogChecksumV2(const ShotLogStoreV2 &store) {
  return shotLogChecksumBytes(store.header);
}

inline uint32_t shotLogChecksumV3(const ShotLogStoreV3 &store) {
  return shotLogChecksumBytes(store.header);
}

inline bool validShotLogStoreV3(const ShotLogStoreV3 &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != 3 ||
      store.header.recordSize != sizeof(ShotLogRecordV3) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksumV3(store)) {
    return false;
  }
  return true;
}

inline bool validShotLogStoreV2(const ShotLogStoreV2 &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != 2 ||
      store.header.recordSize != sizeof(ShotLogRecordV2) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksumV2(store)) {
    return false;
  }
  return true;
}

inline void finalizeShotLogStoreV3(ShotLogStoreV3 &store) {
  store.header.magic = SHOT_LOG_MAGIC;
  store.header.schemaVersion = 3;
  store.header.recordSize = sizeof(ShotLogRecordV3);
  store.header.checksum = 0;
  store.header.checksum = shotLogChecksumV3(store);
}

inline void finalizeShotLogStoreV2(ShotLogStoreV2 &store) {
  store.header.magic = SHOT_LOG_MAGIC;
  store.header.schemaVersion = 2;
  store.header.recordSize = sizeof(ShotLogRecordV2);
  store.header.checksum = 0;
  store.header.checksum = shotLogChecksumV2(store);
}

inline void finalizeShotLogStore(ShotLogStore &store) {
  store.header.magic = SHOT_LOG_MAGIC;
  store.header.schemaVersion = SHOT_LOG_SCHEMA_VERSION;
  store.header.recordSize = sizeof(ShotLogRecord);
  store.header.checksum = 0;
  store.header.checksum = shotLogChecksum(store);
}

inline void migrateShotLogRecordV5ToV6(const ShotLogRecordV5 &source,
                                       ShotLogRecord &dest) {
  dest.id = source.id;
  dest.bootId = source.bootId;
  dest.endedAtMs = source.endedAtMs;
  dest.endedAtUnixSec = source.endedAtUnixSec;
  dest.endedAtLocalSec = source.endedAtLocalSec;
  dest.timezoneOffsetMinutesAtCommit = source.timezoneOffsetMinutesAtCommit;
  dest.durationDs = source.durationDs;
  dest.goalWeightG = source.goalWeightG;
  dest.hasWallTime = source.hasWallTime;
  dest.actualWeightCg = source.actualWeightCg;
  dest.errorCg = source.errorCg;
  dest.offsetUsedCg = source.offsetUsedCg;
  dest.firstDropDs = source.firstDropDs;
  dest.avgFlowCgS = source.avgFlowCgS;
  dest.shotType = source.shotType;
  dest.cutType = source.cutType;
  dest.extractionGuardEnabled = source.extractionGuardEnabled;
  dest.extractionExtended = source.extractionExtended;
  dest.stopDetail = source.stopDetail;
  dest.maxRecoveryWeightCg = source.maxRecoveryWeightCg;
  dest.minBrewTimeDs = source.minBrewTimeDs;
  dest.targetReachedEarlyDs = source.targetReachedEarlyDs;
  dest.actualWeightSource =
      source.actualWeightCg == SHOT_LOG_WEIGHT_MISSING
          ? static_cast<uint8_t>(ActualWeightSource::NONE)
          : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
}

inline void migrateShotLogStoreV5(const ShotLogStoreV5 &legacy,
                                  ShotLogStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = legacy.header.bootId;
  store.header.nextRecordId = legacy.header.nextRecordId;
  store.header.count = legacy.header.count;
  store.header.writeIndex = legacy.header.writeIndex;
  for (size_t index = 0; index < SHOT_LOG_CAPACITY; ++index) {
    migrateShotLogRecordV5ToV6(legacy.records[index], store.records[index]);
  }
  finalizeShotLogStore(store);
}

inline bool validShotLogStoreV5(const ShotLogStoreV5 &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != 5 ||
      store.header.recordSize != sizeof(ShotLogRecordV5) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksumBytes(store.header)) {
    return false;
  }
  return true;
}

inline void migrateShotLogRecordV4ToV5(const ShotLogRecordV4 &source,
                                       ShotLogRecord &dest) {
  dest.id = source.id;
  dest.bootId = source.bootId;
  dest.endedAtMs = source.endedAtMs;
  dest.endedAtUnixSec = source.endedAtUnixSec;
  dest.endedAtLocalSec = source.endedAtLocalSec;
  dest.timezoneOffsetMinutesAtCommit = source.timezoneOffsetMinutesAtCommit;
  dest.durationDs = source.durationDs;
  dest.goalWeightG = source.goalWeightG;
  dest.hasWallTime = source.hasWallTime;
  dest.actualWeightCg = source.actualWeightCg;
  dest.errorCg = source.errorCg;
  dest.offsetUsedCg = source.offsetUsedCg;
  dest.firstDropDs = source.firstDropDs;
  dest.avgFlowCgS = source.avgFlowCgS;
  dest.shotType = source.shotType;
  dest.cutType = source.cutType;
  dest.extractionGuardEnabled = 0;
  dest.extractionExtended = 0;
  dest.stopDetail = static_cast<uint8_t>(ShotLogStopDetail::NORMAL_TARGET);
  dest.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
  dest.minBrewTimeDs = SHOT_LOG_METRIC_MISSING;
  dest.targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
  dest.actualWeightSource =
      source.actualWeightCg == SHOT_LOG_WEIGHT_MISSING
          ? static_cast<uint8_t>(ActualWeightSource::NONE)
          : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
}

inline void migrateShotLogStoreV4(const ShotLogStoreV4 &legacy,
                                  ShotLogStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = legacy.header.bootId;
  store.header.nextRecordId = legacy.header.nextRecordId;
  store.header.count = legacy.header.count;
  store.header.writeIndex = legacy.header.writeIndex;
  for (size_t index = 0; index < SHOT_LOG_CAPACITY; ++index) {
    migrateShotLogRecordV4ToV5(legacy.records[index], store.records[index]);
  }
  finalizeShotLogStore(store);
}

inline bool validShotLogStoreV4(const ShotLogStoreV4 &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != 4 ||
      store.header.recordSize != sizeof(ShotLogRecordV4) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksumBytes(store.header)) {
    return false;
  }
  return true;
}

inline void migrateShotLogStoreV2(const ShotLogStoreV2 &legacy, ShotLogStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = legacy.header.bootId;
  store.header.nextRecordId = legacy.header.nextRecordId;
  store.header.count = legacy.header.count;
  store.header.writeIndex = legacy.header.writeIndex;
  for (size_t index = 0; index < SHOT_LOG_CAPACITY; ++index) {
    const ShotLogRecordV2 &source = legacy.records[index];
    ShotLogRecord &dest = store.records[index];
    dest.id = source.id;
    dest.bootId = source.bootId;
    dest.endedAtMs = source.endedAtMs;
    dest.durationDs = source.durationDs;
    dest.goalWeightG = source.goalWeightG;
    dest.actualWeightCg = source.actualWeightCg;
    dest.errorCg = source.errorCg;
    dest.offsetUsedCg = source.offsetUsedCg;
    dest.firstDropDs = source.firstDropDs;
    dest.avgFlowCgS = source.avgFlowCgS;
    dest.shotType = source.shotType;
    dest.cutType = source.cutType;
    dest.extractionGuardEnabled = 0;
    dest.extractionExtended = 0;
    dest.stopDetail = static_cast<uint8_t>(ShotLogStopDetail::NORMAL_TARGET);
    dest.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
    dest.minBrewTimeDs = SHOT_LOG_METRIC_MISSING;
    dest.targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
    dest.actualWeightSource =
        source.actualWeightCg == SHOT_LOG_WEIGHT_MISSING
            ? static_cast<uint8_t>(ActualWeightSource::NONE)
            : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  }
  finalizeShotLogStore(store);
}

inline void migrateShotLogStoreV3(const ShotLogStoreV3 &legacy, ShotLogStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = legacy.header.bootId;
  store.header.nextRecordId = legacy.header.nextRecordId;
  store.header.count = legacy.header.count;
  store.header.writeIndex = legacy.header.writeIndex;
  for (size_t index = 0; index < SHOT_LOG_CAPACITY; ++index) {
    const ShotLogRecordV3 &source = legacy.records[index];
    ShotLogRecord &dest = store.records[index];
    dest.id = source.id;
    dest.bootId = source.bootId;
    dest.endedAtMs = source.endedAtMs;
    dest.endedAtUnixSec = source.endedAtUnixSec;
    dest.durationDs = source.durationDs;
    dest.goalWeightG = source.goalWeightG;
    dest.actualWeightCg = source.actualWeightCg;
    dest.errorCg = source.errorCg;
    dest.offsetUsedCg = source.offsetUsedCg;
    dest.firstDropDs = source.firstDropDs;
    dest.avgFlowCgS = source.avgFlowCgS;
    dest.shotType = source.shotType;
    dest.cutType = source.cutType;
    dest.extractionGuardEnabled = 0;
    dest.extractionExtended = 0;
    dest.stopDetail = static_cast<uint8_t>(ShotLogStopDetail::NORMAL_TARGET);
    dest.maxRecoveryWeightCg = SHOT_LOG_WEIGHT_MISSING;
    dest.minBrewTimeDs = SHOT_LOG_METRIC_MISSING;
    dest.targetReachedEarlyDs = SHOT_LOG_METRIC_MISSING;
    dest.actualWeightSource =
        source.actualWeightCg == SHOT_LOG_WEIGHT_MISSING
            ? static_cast<uint8_t>(ActualWeightSource::NONE)
            : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  }
  finalizeShotLogStore(store);
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

// Pack the ring into records[0..count) (oldest first) so NVS can store only
// the used prefix instead of the full capacity array.
inline void compactShotLogStore(ShotLogStore &store) {
  const uint16_t count = store.header.count;
  if (count == 0) {
    store.header.writeIndex = 0;
    memset(store.records, 0, sizeof(store.records));
    return;
  }

  ShotLogRecord ordered[SHOT_LOG_CAPACITY];
  size_t index = store.header.writeIndex;
  for (uint16_t step = 0; step < count; ++step) {
    if (index == 0) {
      index = SHOT_LOG_CAPACITY;
    }
    --index;
  }
  for (uint16_t i = 0; i < count; ++i) {
    ordered[i] = store.records[index];
    index = (index + 1U) % SHOT_LOG_CAPACITY;
  }
  memset(store.records, 0, sizeof(store.records));
  memcpy(store.records, ordered, static_cast<size_t>(count) * sizeof(ShotLogRecord));
  store.header.writeIndex =
      static_cast<uint16_t>(count % SHOT_LOG_CAPACITY);
}

inline void resetShotLogStore(ShotLogStore &store, uint32_t bootId) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = bootId == 0 ? 1U : bootId;
  store.header.nextRecordId = 1;
  finalizeShotLogStore(store);
}

class ShotLog {
 public:
  bool load() {
#if defined(SHOT_STOPPER_HOST_TEST)
    resetShotLogStore(store_, 1);
    if (hostStorageValid_) {
      memcpy(&store_, &hostStorage_, sizeof(store_));
    }
    if (!validShotLogStore(store_)) {
      resetShotLogStore(store_, store_.header.bootId);
    }
    return true;
#else
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, true)) {
      resetShotLogStore(store_, 1);
      return false;
    }
    const size_t length = preferences.getBytesLength(SHOT_LOG_KEY);
    bool loaded = false;
    bool needsRewrite = false;
    if (length > 0 && length <= sizeof(store_)) {
      memset(&store_, 0, sizeof(store_));
      if (preferences.getBytes(SHOT_LOG_KEY, &store_, sizeof(store_)) ==
              length &&
          validShotLogStore(store_) &&
          shotLogBlobLengthMatches(store_, length)) {
        loaded = true;
        if (length != shotLogPersistedBytes(store_)) {
          needsRewrite = true;
        }
      }
    }
    if (!loaded && length == sizeof(ShotLogStoreV5)) {
      ShotLogStoreV5 legacy = {};
      if (preferences.getBytes(SHOT_LOG_KEY, &legacy, sizeof(legacy)) ==
              sizeof(legacy) &&
          validShotLogStoreV5(legacy)) {
        migrateShotLogStoreV5(legacy, store_);
        loaded = true;
        needsRewrite = true;
      }
    } else if (!loaded && length == sizeof(ShotLogStoreV4)) {
      ShotLogStoreV4 legacy = {};
      if (preferences.getBytes(SHOT_LOG_KEY, &legacy, sizeof(legacy)) ==
              sizeof(legacy) &&
          validShotLogStoreV4(legacy)) {
        migrateShotLogStoreV4(legacy, store_);
        loaded = true;
        needsRewrite = true;
      }
    } else if (!loaded && length == sizeof(ShotLogStoreV3)) {
      ShotLogStoreV3 legacy = {};
      if (preferences.getBytes(SHOT_LOG_KEY, &legacy, sizeof(legacy)) ==
              sizeof(legacy) &&
          validShotLogStoreV3(legacy)) {
        migrateShotLogStoreV3(legacy, store_);
        loaded = true;
        needsRewrite = true;
      }
    } else if (!loaded && length == sizeof(ShotLogStoreV2)) {
      ShotLogStoreV2 legacy = {};
      if (preferences.getBytes(SHOT_LOG_KEY, &legacy, sizeof(legacy)) ==
              sizeof(legacy) &&
          validShotLogStoreV2(legacy)) {
        migrateShotLogStoreV2(legacy, store_);
        loaded = true;
        needsRewrite = true;
      }
    }
    preferences.end();
    if (!loaded) {
      resetShotLogStore(store_, 1);
    } else if (needsRewrite) {
      save();
    }
    return true;
#endif
  }

  bool save() {
    compactShotLogStore(store_);
    finalizeShotLogStore(store_);
#if defined(SHOT_STOPPER_HOST_TEST)
    memcpy(&hostStorage_, &store_, sizeof(store_));
    hostStorageValid_ = true;
    return true;
#else
    const size_t bytes = shotLogPersistedBytes(store_);
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, false)) {
      return false;
    }
    // Try in-place replace first so a failed write does not erase history.
    size_t written = preferences.putBytes(SHOT_LOG_KEY, &store_, bytes);
    if (written != bytes) {
      // Free the previous blob and retry (NVS copy-on-write often needs the
      // old entry gone before a large/new write can succeed).
      preferences.remove(SHOT_LOG_KEY);
      written = preferences.putBytes(SHOT_LOG_KEY, &store_, bytes);
    }
    preferences.end();
    return written == bytes;
#endif
  }

  void onBoot() {
    if (store_.header.bootId == 0) {
      store_.header.bootId = 1;
    } else if (store_.header.bootId < UINT32_MAX) {
      ++store_.header.bootId;
    }
  }

  uint32_t bootId() const { return store_.header.bootId; }

  bool append(const ShotLogRecord &record) {
    const uint16_t previousWriteIndex = store_.header.writeIndex;
    const uint16_t previousCount = store_.header.count;
    const uint32_t previousNextRecordId = store_.header.nextRecordId;
    const ShotLogRecord overwritten = store_.records[previousWriteIndex];

    ShotLogRecord stored = record;
    stored.id = store_.header.nextRecordId;
    if (store_.header.nextRecordId < UINT32_MAX) {
      ++store_.header.nextRecordId;
    }
    store_.records[store_.header.writeIndex] = stored;
    store_.header.writeIndex =
        static_cast<uint16_t>((store_.header.writeIndex + 1U) %
                              SHOT_LOG_CAPACITY);
    if (store_.header.count < SHOT_LOG_CAPACITY) {
      ++store_.header.count;
    }
    if (save()) {
      return true;
    }
    store_.records[previousWriteIndex] = overwritten;
    store_.header.writeIndex = previousWriteIndex;
    store_.header.count = previousCount;
    store_.header.nextRecordId = previousNextRecordId;
    // If the failed save erased the NVS key, try to put the prior ring back.
    save();
    return false;
  }

  bool removeById(uint32_t id) {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    ShotLogRecord newestFirst[SHOT_LOG_CAPACITY];
    const size_t total = copyNewestFirst(newestFirst, SHOT_LOG_CAPACITY);
    ShotLogRecord kept[SHOT_LOG_CAPACITY];
    size_t keptCount = 0;
    for (size_t index = 0; index < total; ++index) {
      if (newestFirst[index].id != id) {
        kept[keptCount++] = newestFirst[index];
      }
    }
    if (keptCount == total) {
      return false;
    }
    const uint32_t bootId = store_.header.bootId;
    const uint32_t nextRecordId = store_.header.nextRecordId;
    resetShotLogStore(store_, bootId);
    store_.header.nextRecordId = nextRecordId;
    for (size_t index = keptCount; index > 0; --index) {
      store_.records[store_.header.writeIndex] = kept[index - 1];
      store_.header.writeIndex =
          static_cast<uint16_t>((store_.header.writeIndex + 1U) %
                                SHOT_LOG_CAPACITY);
      if (store_.header.count < SHOT_LOG_CAPACITY) {
        ++store_.header.count;
      }
    }
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  bool clear() {
    const uint32_t bootId = store_.header.bootId;
    resetShotLogStore(store_, bootId);
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  size_t count() const { return store_.header.count; }

  size_t copyNewestFirst(ShotLogRecord *output, size_t capacity) const {
    if (output == nullptr || capacity == 0 || store_.header.count == 0) {
      return 0;
    }
    const size_t toCopy =
        store_.header.count < capacity ? store_.header.count : capacity;
    size_t index = store_.header.writeIndex;
    for (size_t copied = 0; copied < toCopy; ++copied) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      output[copied] = store_.records[index];
    }
    return toCopy;
  }

 private:
  static constexpr const char *SHOT_LOG_NAMESPACE = "shotlog";
  static constexpr const char *SHOT_LOG_KEY = "records";

  ShotLogStore store_;

#if defined(SHOT_STOPPER_HOST_TEST)
  static ShotLogStore hostStorage_;
  static bool hostStorageValid_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST)
ShotLogStore ShotLog::hostStorage_;
bool ShotLog::hostStorageValid_ = false;
#endif

inline int16_t shotLogWeightToCentigrams(float weightG) {
  if (!isfinite(weightG)) {
    return SHOT_LOG_WEIGHT_MISSING;
  }
  const int32_t centigrams = weightToCentigrams(weightG);
  if (centigrams == INT32_MAX || centigrams < INT16_MIN ||
      centigrams > INT16_MAX) {
    return SHOT_LOG_WEIGHT_MISSING;
  }
  return static_cast<int16_t>(centigrams);
}

}  // namespace shotstopper
