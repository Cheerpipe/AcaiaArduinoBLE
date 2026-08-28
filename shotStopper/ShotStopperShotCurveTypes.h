#pragma once

// Compact per-shot weight sparkline (2 s grid + event vertices, RAM sampler +
// flash sidecar). Not stored in NVS ShotLogRecord (locked at 48 bytes).

#include "ShotStopperShotLogTypes.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace shotstopper {

constexpr uint32_t SHOT_CURVE_MAGIC = 0x53435256U;  // "SCRV"
// Current shot-curve schema. V1 is the baseline — no upgrade from prior layouts.
constexpr uint16_t SHOT_CURVE_SCHEMA_VERSION = 1;
constexpr uint32_t SHOT_CURVE_INTERVAL_MS = 2000;
constexpr uint8_t SHOT_CURVE_INTERVAL_S = 2;
// 0 + 30×2 s covers HARD_MAX_CIRCUIT_CLOSED_MS (60 s).
constexpr size_t SHOT_CURVE_MAX_POINTS = 31;
constexpr size_t SHOT_CURVE_CAPACITY = SHOT_LOG_CAPACITY;

struct ShotCurveEvent {
  uint16_t atDs;
  int16_t weightCg;
};

static_assert(sizeof(ShotCurveEvent) == 4,
              "ShotCurveEvent packing is part of the flash sidecar schema");

inline ShotCurveEvent missingShotCurveEvent() {
  ShotCurveEvent event = {};
  event.atDs = SHOT_LOG_METRIC_MISSING;
  event.weightCg = SHOT_LOG_WEIGHT_MISSING;
  return event;
}

inline bool shotCurveEventPresent(const ShotCurveEvent &event) {
  return event.atDs != SHOT_LOG_METRIC_MISSING &&
         !shotLogWeightIsMissing(event.weightCg);
}

struct ShotCurveRecord {
  uint32_t shotId;
  uint8_t count;
  uint8_t intervalS;
  uint16_t atmClearedDs;
  ShotCurveEvent firstDrop;
  ShotCurveEvent extended;
  ShotCurveEvent atm;
  ShotCurveEvent ended;
  int16_t weightCg[SHOT_CURVE_MAX_POINTS];
};

inline ShotCurveRecord emptyShotCurveRecord() {
  ShotCurveRecord curve = {};
  curve.intervalS = SHOT_CURVE_INTERVAL_S;
  curve.atmClearedDs = SHOT_LOG_METRIC_MISSING;
  curve.firstDrop = missingShotCurveEvent();
  curve.extended = missingShotCurveEvent();
  curve.atm = missingShotCurveEvent();
  curve.ended = missingShotCurveEvent();
  return curve;
}

static_assert(sizeof(ShotCurveRecord) == 88,
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

static_assert(sizeof(ShotCurveStore) ==
                  sizeof(ShotCurveHeader) +
                      sizeof(ShotCurveRecord) * SHOT_CURVE_CAPACITY,
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
  uint8_t count = 0;
  uint16_t atmClearedDs = SHOT_LOG_METRIC_MISSING;
  ShotCurveEvent firstDrop = missingShotCurveEvent();
  ShotCurveEvent extended = missingShotCurveEvent();
  ShotCurveEvent atm = missingShotCurveEvent();
  ShotCurveEvent ended = missingShotCurveEvent();
  int16_t weightCg[SHOT_CURVE_MAX_POINTS] = {};

  void reset(uint32_t startedAtMs) {
    startMs = startedAtMs;
    count = 0;
    atmClearedDs = SHOT_LOG_METRIC_MISSING;
    firstDrop = missingShotCurveEvent();
    extended = missingShotCurveEvent();
    atm = missingShotCurveEvent();
    ended = missingShotCurveEvent();
    memset(weightCg, 0, sizeof(weightCg));
  }

  bool elapsedDsFrom(uint32_t atMs, uint16_t &outDs) const {
    if (startMs == 0) {
      return false;
    }
    if (static_cast<int32_t>(atMs - startMs) < 0) {
      return false;
    }
    const uint32_t elapsedDs = (atMs - startMs) / 100U;
    if (elapsedDs > 0xFFFFU) {
      return false;
    }
    outDs = static_cast<uint16_t>(elapsedDs);
    return true;
  }

  void latchEvent(ShotCurveEvent &event, uint32_t atMs, float weight) {
    if (shotCurveEventPresent(event) || startMs == 0) {
      return;
    }
    uint16_t ds = 0;
    if (!elapsedDsFrom(atMs, ds)) {
      return;
    }
    const int16_t cg = shotLogWeightToCentigrams(weight);
    if (shotLogWeightIsMissing(cg)) {
      return;
    }
    event.atDs = ds;
    event.weightCg = cg;
  }

  void latchFirstDrop(uint32_t atMs, float weight) {
    latchEvent(firstDrop, atMs, weight);
  }

  void latchExtended(uint32_t atMs, float weight) {
    latchEvent(extended, atMs, weight);
  }

  void latchAtm(uint32_t atMs, float weight) {
    if (shotCurveEventPresent(atm) &&
        atmClearedDs == SHOT_LOG_METRIC_MISSING) {
      return;
    }
    atm = missingShotCurveEvent();
    atmClearedDs = SHOT_LOG_METRIC_MISSING;
    latchEvent(atm, atMs, weight);
  }

  void latchAtmCleared(uint32_t atMs) {
    if (!shotCurveEventPresent(atm) ||
        atmClearedDs != SHOT_LOG_METRIC_MISSING) {
      return;
    }
    uint16_t ds = 0;
    if (!elapsedDsFrom(atMs, ds)) {
      return;
    }
    atmClearedDs = ds;
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

  void captureEnd(uint32_t atMs, float weight = NAN) {
    if (startMs == 0) {
      return;
    }
    const uint32_t elapsedMs =
        static_cast<int32_t>(atMs - startMs) < 0 ? 0U : (atMs - startMs);
    size_t index = elapsedMs / SHOT_CURVE_INTERVAL_MS;
    if (index >= SHOT_CURVE_MAX_POINTS) {
      index = SHOT_CURVE_MAX_POINTS - 1U;
    }
    if (count == 0) {
      const int16_t cg = isfinite(weight)
                             ? shotLogWeightToCentigrams(weight)
                             : SHOT_LOG_WEIGHT_MISSING;
      if (!shotLogWeightIsMissing(cg)) {
        weightCg[0] = cg;
        count = 1;
      } else {
        return;
      }
    }
    const int16_t hold = weightCg[count - 1U];
    while (count <= index && count < SHOT_CURVE_MAX_POINTS) {
      weightCg[count] = hold;
      ++count;
    }
    float endWeight = weight;
    if (!isfinite(endWeight)) {
      endWeight = static_cast<float>(hold) / 100.0f;
    }
    latchEvent(ended, atMs, endWeight);
  }

  ShotCurveRecord snapshot(uint32_t shotId = 0) const {
    ShotCurveRecord record = {};
    record.shotId = shotId;
    record.count = count;
    record.intervalS = SHOT_CURVE_INTERVAL_S;
    record.atmClearedDs = atmClearedDs;
    record.firstDrop = firstDrop;
    record.extended = extended;
    record.atm = atm;
    record.ended = ended;
    memcpy(record.weightCg, weightCg, sizeof(weightCg));
    return record;
  }
};

// The settled weight is observed during drip delay, but the shot duration is
// defined by the machine circuit opening. Put that settled value at the
// already-captured end vertex instead of extending the curve through drip
// delay. When the end lands exactly on the compact grid, keep that grid point
// consistent with the event vertex too.
inline bool settleShotCurveEndWeight(ShotCurveRecord &curve, float weight) {
  if (!shotCurveEventPresent(curve.ended) || !isfinite(weight)) {
    return false;
  }
  const int16_t cg = shotLogWeightToCentigrams(weight);
  if (shotLogWeightIsMissing(cg)) {
    return false;
  }
  curve.ended.weightCg = cg;

  const uint16_t intervalDs = static_cast<uint16_t>(curve.intervalS) * 10U;
  if (intervalDs != 0U && curve.ended.atDs % intervalDs == 0U) {
    const size_t index = curve.ended.atDs / intervalDs;
    if (index < curve.count && index < SHOT_CURVE_MAX_POINTS) {
      curve.weightCg[index] = cg;
    }
  }
  return true;
}

inline bool formatShotCurveMetricS(char *out, size_t capacity, const char *key,
                                   uint16_t ds) {
  if (out == nullptr || capacity < 8 || key == nullptr) {
    return false;
  }
  if (ds == SHOT_LOG_METRIC_MISSING) {
    return snprintf(out, capacity, ",\"%s\":null", key) > 0;
  }
  return snprintf(out, capacity, ",\"%s\":%.1f", key,
                  static_cast<double>(ds) / 10.0) > 0;
}

inline bool formatShotCurveMetricCg(char *out, size_t capacity, const char *key,
                                    int16_t cg) {
  if (out == nullptr || capacity < 8 || key == nullptr) {
    return false;
  }
  if (shotLogWeightIsMissing(cg)) {
    return snprintf(out, capacity, ",\"%s\":null", key) > 0;
  }
  return snprintf(out, capacity, ",\"%s\":%d", key, static_cast<int>(cg)) > 0;
}

// JSON object body: wCg, wDtS, event vertices. No surrounding braces.
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
  char piece[40] = {};
  snprintf(piece, sizeof(piece), "],\"wDtS\":%u",
           static_cast<unsigned>(curve.intervalS == 0 ? SHOT_CURVE_INTERVAL_S
                                                      : curve.intervalS));
  if (!append(piece)) {
    return false;
  }
  auto appendMetricS = [&](const char *key, uint16_t ds) -> bool {
    return formatShotCurveMetricS(piece, sizeof(piece), key, ds) &&
           append(piece);
  };
  auto appendMetricCg = [&](const char *key, int16_t cg) -> bool {
    return formatShotCurveMetricCg(piece, sizeof(piece), key, cg) &&
           append(piece);
  };
  return appendMetricS("dropS", curve.firstDrop.atDs) &&
         appendMetricCg("dropCg", curve.firstDrop.weightCg) &&
         appendMetricS("extendedS", curve.extended.atDs) &&
         appendMetricCg("extCg", curve.extended.weightCg) &&
         appendMetricS("atmS", curve.atm.atDs) &&
         appendMetricCg("atmCg", curve.atm.weightCg) &&
         appendMetricS("atmClearedS", curve.atmClearedDs) &&
         appendMetricS("endS", curve.ended.atDs) &&
         appendMetricCg("endCg", curve.ended.weightCg);
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

inline void copyShotCurveRecordToStatusFields(
    const ShotCurveRecord &curve, uint8_t &count, uint8_t &intervalS,
    uint16_t &firstDropDs, int16_t &firstDropCg, uint16_t &extendedDs,
    int16_t &extendedCg, uint16_t &atmDs, int16_t &atmCg,
    uint16_t &atmClearedDs, uint16_t &endedDs, int16_t &endedCg,
    int16_t *weightCg, size_t weightCapacity) {
  count = curve.count;
  intervalS = curve.intervalS == 0 ? SHOT_CURVE_INTERVAL_S : curve.intervalS;
  firstDropDs = curve.firstDrop.atDs;
  firstDropCg = curve.firstDrop.weightCg;
  extendedDs = curve.extended.atDs;
  extendedCg = curve.extended.weightCg;
  atmDs = curve.atm.atDs;
  atmCg = curve.atm.weightCg;
  atmClearedDs = curve.atmClearedDs;
  endedDs = curve.ended.atDs;
  endedCg = curve.ended.weightCg;
  if (weightCg == nullptr || weightCapacity == 0) {
    return;
  }
  const size_t copy =
      weightCapacity < SHOT_CURVE_MAX_POINTS ? weightCapacity
                                             : SHOT_CURVE_MAX_POINTS;
  memcpy(weightCg, curve.weightCg, copy * sizeof(int16_t));
}

inline ShotCurveRecord shotCurveRecordFromStatusFields(
    uint8_t count, uint8_t intervalS, uint16_t firstDropDs, int16_t firstDropCg,
    uint16_t extendedDs, int16_t extendedCg, uint16_t atmDs, int16_t atmCg,
    uint16_t atmClearedDs, uint16_t endedDs, int16_t endedCg,
    const int16_t *weightCg, size_t weightCount) {
  ShotCurveRecord curve = {};
  curve.count = count;
  curve.intervalS = intervalS == 0 ? SHOT_CURVE_INTERVAL_S : intervalS;
  curve.atmClearedDs = atmClearedDs;
  curve.firstDrop.atDs = firstDropDs;
  curve.firstDrop.weightCg = firstDropCg;
  curve.extended.atDs = extendedDs;
  curve.extended.weightCg = extendedCg;
  curve.atm.atDs = atmDs;
  curve.atm.weightCg = atmCg;
  curve.ended.atDs = endedDs;
  curve.ended.weightCg = endedCg;
  if (weightCg != nullptr && weightCount > 0) {
    const size_t copy =
        weightCount < SHOT_CURVE_MAX_POINTS ? weightCount
                                            : SHOT_CURVE_MAX_POINTS;
    memcpy(curve.weightCg, weightCg, copy * sizeof(int16_t));
  }
  return curve;
}

}  // namespace shotstopper
