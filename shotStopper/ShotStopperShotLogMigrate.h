#pragma once

// Shot-log schema migrations placeholder.
//
// Current on-disk schema is SHOT_LOG_SCHEMA_VERSION (V1). There is no upgrade
// path from any prior layout: unrecognized blobs decode as INVALID.
//
// When bumping SHOT_LOG_SCHEMA_VERSION:
// 1. Keep the previous record/store layout as ShotLogRecordV<N> / StoreV<N>.
// 2. Add migrateShotLogStoreV<N>(...) and a validShotLogStoreV<N>(...) check.
// 3. Extend decodeShotLogBlob() to accept that length/version and return
//    ShotLogDecodeStatus::MIGRATED after rewriting into the current store.
// 4. Cover the path in persistence_host_test.cpp.

#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

enum class ShotLogDecodeStatus : uint8_t {
  CURRENT = 0,
  MIGRATED = 1,
  INVALID = 2,
};

// `bytes` may alias `out` for the current schema. Future migrations that
// memset the destination must reject aliasing for those legacy sizes.
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
  return ShotLogDecodeStatus::INVALID;
}

}  // namespace shotstopper
