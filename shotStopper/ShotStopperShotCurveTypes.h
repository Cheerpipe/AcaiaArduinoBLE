#pragma once

// Compact per-shot weight sparkline (2 s grid, RAM sampler + flash sidecar).
// Not stored in NVS ShotLogRecord (locked at 48 bytes).

#include "ShotStopperShotLogTypes.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t SHOT_CURVE_MAGIC = 0x53435256U;  // "SCRV"
constexpr uint16_t SHOT_CURVE_SCHEMA_VERSION = 1;
constexpr uint32_t SHOT_CURVE_INTERVAL_MS = 2000;
constexpr uint8_t SHOT_CURVE_INTERVAL_S = 2;
// 0 + 30×2 s covers HARD_MAX_CN9_CLOSED_MS (60 s).
constexpr size_t SHOT_CURVE_MAX_POINTS = 31;
constexpr size_t SHOT_CURVE_CAPACITY = SHOT_LOG_CAPACITY;

struct ShotCurveRecord {
  uint32_t shotId;
  uint16_t extendedEnteredDs;
  uint8_t count;
  uint8_t intervalS;
  int16_t weightCg[SHOT_CURVE_MAX_POINTS];
  uint16_t reserved;
};

static_assert(sizeof(ShotCurveRecord) == 72,
              "ShotCurveRecord packing is part of the flash sidecar schema");

struct ShotCurveHeader {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t recordSize;
  uint32_t generation;
  uint16_t count;
  uint16_t writeIndex;
  uint32_t checksum;
};

static_assert(sizeof(ShotCurveHeader) == 20,
              "ShotCurveHeader packing is part of the flash sidecar schema");

struct ShotCurveStore {
  ShotCurveHeader header;
  ShotCurveRecord records[SHOT_CURVE_CAPACITY];
};

static_assert(sizeof(ShotCurveStore) == 20 + 72 * SHOT_CURVE_CAPACITY,
              "ShotCurveStore size must stay within FLASH_IO_SCRATCH_BYTES");

inline uint32_t shotCurveChecksum(const ShotCurveStore &store) {
  uint32_t crc = crc32Update(
      0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(&store.header),
      offsetof(ShotCurveHeader, checksum));
  if (store.header.count > 0) {
    crc = crc32Update(crc, reinterpret_cast<const uint8_t *>(store.records),
                      static_cast<size_t>(store.header.count) *
                          sizeof(ShotCurveRecord));
  }
  return ~crc;
}

inline void finalizeShotCurveStore(ShotCurveStore &store) {
  store.header.magic = SHOT_CURVE_MAGIC;
  store.header.schemaVersion = SHOT_CURVE_SCHEMA_VERSION;
  store.header.recordSize = sizeof(ShotCurveRecord);
  store.header.checksum = 0;
  store.header.checksum = shotCurveChecksum(store);
}

inline bool validShotCurveStore(const ShotCurveStore &store) {
  return store.header.magic == SHOT_CURVE_MAGIC &&
         store.header.schemaVersion == SHOT_CURVE_SCHEMA_VERSION &&
         store.header.recordSize == sizeof(ShotCurveRecord) &&
         store.header.count <= SHOT_CURVE_CAPACITY &&
         store.header.writeIndex < SHOT_CURVE_CAPACITY &&
         store.header.checksum == shotCurveChecksum(store);
}

inline void resetShotCurveStore(ShotCurveStore &store) {
  memset(&store, 0, sizeof(store));
  store.header.generation = 1;
  finalizeShotCurveStore(store);
}

inline void compactShotCurveStoreInto(ShotCurveStore &store,
                                      ShotCurveStore &scratch) {
  const uint16_t count = store.header.count;
  if (count == 0) {
    store.header.writeIndex = 0;
    memset(store.records, 0, sizeof(store.records));
    return;
  }

  size_t index = store.header.writeIndex;
  for (uint16_t step = 0; step < count; ++step) {
    if (index == 0) {
      index = SHOT_CURVE_CAPACITY;
    }
    --index;
  }
  for (uint16_t i = 0; i < count; ++i) {
    scratch.records[i] = store.records[index];
    index = (index + 1U) % SHOT_CURVE_CAPACITY;
  }
  memset(store.records, 0, sizeof(store.records));
  memcpy(store.records, scratch.records,
         static_cast<size_t>(count) * sizeof(ShotCurveRecord));
  store.header.writeIndex =
      static_cast<uint16_t>(count % SHOT_CURVE_CAPACITY);
}

struct ShotCurveSampler {
  uint32_t startMs = 0;
  uint16_t extendedEnteredDs = SHOT_LOG_METRIC_MISSING;
  uint8_t count = 0;
  int16_t weightCg[SHOT_CURVE_MAX_POINTS] = {};

  void reset(uint32_t startedAtMs) {
    startMs = startedAtMs;
    extendedEnteredDs = SHOT_LOG_METRIC_MISSING;
    count = 0;
    memset(weightCg, 0, sizeof(weightCg));
  }

  void latchExtended(uint32_t atMs) {
    if (extendedEnteredDs != SHOT_LOG_METRIC_MISSING || startMs == 0) {
      return;
    }
    if (static_cast<int32_t>(atMs - startMs) < 0) {
      return;
    }
    const uint32_t elapsedDs = (atMs - startMs) / 100U;
    if (elapsedDs > 0xFFFFU) {
      return;
    }
    extendedEnteredDs = static_cast<uint16_t>(elapsedDs);
  }

  void accept(float weight, uint32_t receivedAtMs) {
    if (startMs == 0 || !isfinite(weight)) {
      return;
    }
    if (static_cast<int32_t>(receivedAtMs - startMs) < 0) {
      return;
    }
    const int16_t cg = shotLogWeightToCentigrams(weight);
    if (shotLogWeightIsMissing(cg)) {
      return;
    }
    const uint32_t elapsedMs = receivedAtMs - startMs;
    size_t index = elapsedMs / SHOT_CURVE_INTERVAL_MS;
    if (index >= SHOT_CURVE_MAX_POINTS) {
      index = SHOT_CURVE_MAX_POINTS - 1U;
    }
    if (count == 0) {
      weightCg[0] = index == 0 ? cg : 0;
      count = 1;
    }
    while (count <= index && count < SHOT_CURVE_MAX_POINTS) {
      weightCg[count] = cg;
      ++count;
    }
    if (index < count) {
      weightCg[index] = cg;
    }
  }

  void captureEnd(uint32_t atMs) {
    if (startMs == 0 || count == 0) {
      return;
    }
    const uint32_t elapsedMs =
        static_cast<int32_t>(atMs - startMs) < 0 ? 0U : (atMs - startMs);
    size_t index = elapsedMs / SHOT_CURVE_INTERVAL_MS;
    if (index >= SHOT_CURVE_MAX_POINTS) {
      index = SHOT_CURVE_MAX_POINTS - 1U;
    }
    while (count <= index && count < SHOT_CURVE_MAX_POINTS) {
      weightCg[count] = weightCg[count - 1U];
      ++count;
    }
  }

  ShotCurveRecord snapshot(uint32_t shotId = 0) const {
    ShotCurveRecord record = {};
    record.shotId = shotId;
    record.extendedEnteredDs = extendedEnteredDs;
    record.count = count;
    record.intervalS = SHOT_CURVE_INTERVAL_S;
    memcpy(record.weightCg, weightCg, sizeof(weightCg));
    return record;
  }
};

// JSON object body: wCg, wDtS, extendedS. No surrounding braces.
inline bool formatShotCurveJsonBody(char *out, size_t capacity,
                                    const ShotCurveRecord &curve) {
  if (out == nullptr || capacity < 32) {
    return false;
  }
  size_t used = 0;
  auto append = [&](const char *text) -> bool {
    const size_t length = strlen(text);
    if (used + length + 1U > capacity) {
      return false;
    }
    memcpy(out + used, text, length);
    used += length;
    out[used] = '\0';
    return true;
  };
  if (!append("\"wCg\":[")) {
    return false;
  }
  const uint8_t count =
      curve.count > SHOT_CURVE_MAX_POINTS ? SHOT_CURVE_MAX_POINTS : curve.count;
  for (uint8_t i = 0; i < count; ++i) {
    char item[12] = {};
    snprintf(item, sizeof(item), "%s%d", i == 0 ? "" : ",",
             static_cast<int>(curve.weightCg[i]));
    if (!append(item)) {
      return false;
    }
  }
  char tail[48] = {};
  if (curve.extendedEnteredDs == SHOT_LOG_METRIC_MISSING) {
    snprintf(tail, sizeof(tail), "],\"wDtS\":%u,\"extendedS\":null",
             static_cast<unsigned>(curve.intervalS == 0 ? SHOT_CURVE_INTERVAL_S
                                                        : curve.intervalS));
  } else {
    snprintf(tail, sizeof(tail), "],\"wDtS\":%u,\"extendedS\":%.1f",
             static_cast<unsigned>(curve.intervalS == 0 ? SHOT_CURVE_INTERVAL_S
                                                        : curve.intervalS),
             static_cast<double>(curve.extendedEnteredDs) / 10.0);
  }
  return append(tail);
}

inline const ShotCurveRecord *findShotCurveById(const ShotCurveRecord *curves,
                                                size_t count, uint32_t shotId) {
  if (curves == nullptr || shotId == 0) {
    return nullptr;
  }
  for (size_t i = 0; i < count; ++i) {
    if (curves[i].shotId == shotId) {
      return &curves[i];
    }
  }
  return nullptr;
}

}  // namespace shotstopper
