#pragma once

#include "ShotStopperDomain.h"

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_HOST_TEST)
// Host tests keep the shot log in memory only.
#else
#include <Preferences.h>
#endif

namespace shotstopper {

constexpr uint32_t SHOT_LOG_MAGIC = 0x534C4F47U;  // "SLOG"
constexpr uint16_t SHOT_LOG_SCHEMA_VERSION = 3;
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

struct ShotLogStoreV2 {
  ShotLogHeader header;
  ShotLogRecordV2 records[SHOT_LOG_CAPACITY];
};

static_assert(sizeof(ShotLogRecordV2) + sizeof(uint32_t) == sizeof(ShotLogRecord),
              "v3 shot log records extend v2 with endedAtUnixSec");

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
    dest.endedAtUnixSec = 0;
    dest.durationDs = source.durationDs;
    dest.goalWeightG = source.goalWeightG;
    dest.actualWeightCg = source.actualWeightCg;
    dest.errorCg = source.errorCg;
    dest.offsetUsedCg = source.offsetUsedCg;
    dest.firstDropDs = source.firstDropDs;
    dest.avgFlowCgS = source.avgFlowCgS;
    dest.shotType = source.shotType;
    dest.cutType = source.cutType;
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

inline void resetShotLogStore(ShotLogStore &store, uint32_t bootId) {
  memset(&store, 0, sizeof(store));
  store.header.bootId = bootId == 0 ? 1U : bootId;
  store.header.nextRecordId = 1;
  finalizeShotLogStore(store);
}

class ShotLog {
 public:
  bool load() {
#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_HOST_TEST)
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
    if (length == sizeof(store_)) {
      if (preferences.getBytes(SHOT_LOG_KEY, &store_, sizeof(store_)) ==
              sizeof(store_) &&
          validShotLogStore(store_)) {
        loaded = true;
      }
    } else if (length == sizeof(ShotLogStoreV2)) {
      ShotLogStoreV2 legacy = {};
      if (preferences.getBytes(SHOT_LOG_KEY, &legacy, sizeof(legacy)) ==
              sizeof(legacy) &&
          validShotLogStoreV2(legacy)) {
        migrateShotLogStoreV2(legacy, store_);
        loaded = true;
      }
    }
    preferences.end();
    if (!loaded) {
      resetShotLogStore(store_, 1);
    } else if (length == sizeof(ShotLogStoreV2)) {
      save();
    }
    return true;
#endif
  }

  bool save() {
    finalizeShotLogStore(store_);
#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_HOST_TEST)
    memcpy(&hostStorage_, &store_, sizeof(store_));
    hostStorageValid_ = true;
    return true;
#else
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, false)) {
      return false;
    }
    const size_t written =
        preferences.putBytes(SHOT_LOG_KEY, &store_, sizeof(store_));
    preferences.end();
    return written == sizeof(store_);
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
    return save();
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
    return save();
  }

  bool clear() {
    const uint32_t bootId = store_.header.bootId;
    resetShotLogStore(store_, bootId);
    return save();
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

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_HOST_TEST)
  static ShotLogStore hostStorage_;
  static bool hostStorageValid_;
#endif
};

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST) || defined(SHOT_STOPPER_HOST_TEST)
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
