#pragma once

// Pure shot-log schema migrations. No Preferences / NVS.

#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

constexpr uint16_t SHOT_LOG_SCHEMA_VERSION_V6 = 6;

enum class ShotLogDecodeStatus : uint8_t {
  CURRENT = 0,
  MIGRATED = 1,
  INVALID = 2,
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
  dest.actualWeightCg = normalizeLegacyShotLogWeight(source.actualWeightCg);
  dest.errorCg = normalizeLegacyShotLogWeight(source.errorCg);
  dest.offsetUsedCg = normalizeLegacyShotLogWeight(source.offsetUsedCg);
  dest.firstDropDs = source.firstDropDs;
  dest.avgFlowCgS = source.avgFlowCgS;
  dest.shotType = source.shotType;
  dest.cutType = source.cutType;
  dest.extractionGuardEnabled = source.extractionGuardEnabled;
  dest.extractionExtended = source.extractionExtended;
  dest.stopDetail = source.stopDetail;
  dest.maxRecoveryWeightCg =
      normalizeLegacyShotLogWeight(source.maxRecoveryWeightCg);
  dest.minBrewTimeDs = source.minBrewTimeDs;
  dest.targetReachedEarlyDs = source.targetReachedEarlyDs;
  dest.actualWeightSource =
      shotLogWeightIsMissing(source.actualWeightCg)
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

inline bool validShotLogStoreV6(const ShotLogStore &store) {
  if (store.header.magic != SHOT_LOG_MAGIC ||
      store.header.schemaVersion != SHOT_LOG_SCHEMA_VERSION_V6 ||
      store.header.recordSize != sizeof(ShotLogRecord) ||
      store.header.count > SHOT_LOG_CAPACITY ||
      store.header.writeIndex >= SHOT_LOG_CAPACITY ||
      store.header.checksum != shotLogChecksumBytes(store.header)) {
    return false;
  }
  return true;
}

inline void migrateShotLogStoreV6(ShotLogStore &store) {
  for (uint16_t index = 0; index < store.header.count; ++index) {
    ShotLogRecord &record = store.records[index];
    record.actualWeightCg =
        normalizeLegacyShotLogWeight(record.actualWeightCg);
    record.errorCg = normalizeLegacyShotLogWeight(record.errorCg);
    record.offsetUsedCg = normalizeLegacyShotLogWeight(record.offsetUsedCg);
    record.maxRecoveryWeightCg =
        normalizeLegacyShotLogWeight(record.maxRecoveryWeightCg);
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
  dest.actualWeightCg = normalizeLegacyShotLogWeight(source.actualWeightCg);
  dest.errorCg = normalizeLegacyShotLogWeight(source.errorCg);
  dest.offsetUsedCg = normalizeLegacyShotLogWeight(source.offsetUsedCg);
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
      shotLogWeightIsMissing(source.actualWeightCg)
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

inline void migrateShotLogStoreV2(const ShotLogStoreV2 &legacy,
                                  ShotLogStore &store) {
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
    dest.actualWeightCg = normalizeLegacyShotLogWeight(source.actualWeightCg);
    dest.errorCg = normalizeLegacyShotLogWeight(source.errorCg);
    dest.offsetUsedCg = normalizeLegacyShotLogWeight(source.offsetUsedCg);
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
        shotLogWeightIsMissing(source.actualWeightCg)
            ? static_cast<uint8_t>(ActualWeightSource::NONE)
            : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  }
  finalizeShotLogStore(store);
}

inline void migrateShotLogStoreV3(const ShotLogStoreV3 &legacy,
                                  ShotLogStore &store) {
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
    dest.actualWeightCg = normalizeLegacyShotLogWeight(source.actualWeightCg);
    dest.errorCg = normalizeLegacyShotLogWeight(source.errorCg);
    dest.offsetUsedCg = normalizeLegacyShotLogWeight(source.offsetUsedCg);
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
        shotLogWeightIsMissing(source.actualWeightCg)
            ? static_cast<uint8_t>(ActualWeightSource::NONE)
            : static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  }
  finalizeShotLogStore(store);
}

// `bytes` must not alias `out` for v2–v5 (migrate memsets dest). v6/current
// may alias. Caller typically reads into flash I/O scratch and decodes to
// the live store.
inline ShotLogDecodeStatus decodeShotLogBlob(const void *bytes, size_t length,
                                             ShotLogStore &out) {
  if (bytes == nullptr || length == 0 || length > sizeof(ShotLogStore)) {
    return ShotLogDecodeStatus::INVALID;
  }

  const auto *asCurrent = reinterpret_cast<const ShotLogStore *>(bytes);
  if (validShotLogStore(*asCurrent) &&
      shotLogBlobLengthMatches(*asCurrent, length)) {
    if (&out != asCurrent) {
      out = *asCurrent;
    }
    return ShotLogDecodeStatus::CURRENT;
  }
  if (validShotLogStoreV6(*asCurrent) &&
      shotLogBlobLengthMatches(*asCurrent, length)) {
    if (&out != asCurrent) {
      out = *asCurrent;
    }
    migrateShotLogStoreV6(out);
    return ShotLogDecodeStatus::MIGRATED;
  }

  if (length == sizeof(ShotLogStoreV5)) {
    const auto *legacy = reinterpret_cast<const ShotLogStoreV5 *>(bytes);
    if (static_cast<const void *>(legacy) != static_cast<const void *>(&out) &&
        validShotLogStoreV5(*legacy)) {
      migrateShotLogStoreV5(*legacy, out);
      return ShotLogDecodeStatus::MIGRATED;
    }
  }
  if (length == sizeof(ShotLogStoreV4)) {
    const auto *legacy = reinterpret_cast<const ShotLogStoreV4 *>(bytes);
    if (static_cast<const void *>(legacy) != static_cast<const void *>(&out) &&
        validShotLogStoreV4(*legacy)) {
      migrateShotLogStoreV4(*legacy, out);
      return ShotLogDecodeStatus::MIGRATED;
    }
  }
  if (length == sizeof(ShotLogStoreV3)) {
    const auto *legacy = reinterpret_cast<const ShotLogStoreV3 *>(bytes);
    if (static_cast<const void *>(legacy) != static_cast<const void *>(&out) &&
        validShotLogStoreV3(*legacy)) {
      migrateShotLogStoreV3(*legacy, out);
      return ShotLogDecodeStatus::MIGRATED;
    }
  }
  if (length == sizeof(ShotLogStoreV2)) {
    const auto *legacy = reinterpret_cast<const ShotLogStoreV2 *>(bytes);
    if (static_cast<const void *>(legacy) != static_cast<const void *>(&out) &&
        validShotLogStoreV2(*legacy)) {
      migrateShotLogStoreV2(*legacy, out);
      return ShotLogDecodeStatus::MIGRATED;
    }
  }
  return ShotLogDecodeStatus::INVALID;
}

}  // namespace shotstopper
