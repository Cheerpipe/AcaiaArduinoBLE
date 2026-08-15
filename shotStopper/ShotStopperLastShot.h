#pragma once

#include "ShotStopperDomain.h"

#if defined(SHOT_STOPPER_HOST_TEST)
// State-machine host tests keep the last-shot snapshot in memory only.
#elif !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <Preferences.h>
#endif
// Persistence host tests provide Preferences via persistence_host_stubs.h.

namespace shotstopper {

constexpr uint32_t LAST_SHOT_MAGIC = 0x4C534854U;  // "LSHT"
constexpr uint16_t LAST_SHOT_SCHEMA_VERSION = 3;

struct LastShotBlob {
  uint32_t magic = LAST_SHOT_MAGIC;
  uint16_t schemaVersion = LAST_SHOT_SCHEMA_VERSION;
  uint16_t structureSize = 0;
  PersistedLastShot shot = {};
  uint32_t checksum = 0;
};

inline uint32_t lastShotChecksum(const LastShotBlob &blob) {
  return crc32(reinterpret_cast<const uint8_t *>(&blob),
               offsetof(LastShotBlob, checksum));
}

inline void finalizeLastShotBlob(LastShotBlob &blob) {
  blob.magic = LAST_SHOT_MAGIC;
  blob.schemaVersion = LAST_SHOT_SCHEMA_VERSION;
  blob.structureSize = sizeof(LastShotBlob);
  blob.checksum = 0;
  blob.checksum = lastShotChecksum(blob);
}

inline bool validLastShotBlob(const LastShotBlob &blob) {
  return blob.magic == LAST_SHOT_MAGIC &&
         blob.schemaVersion == LAST_SHOT_SCHEMA_VERSION &&
         blob.structureSize == sizeof(LastShotBlob) &&
         blob.checksum == lastShotChecksum(blob);
}

inline void resetLastShotBlob(LastShotBlob &blob) {
  blob = LastShotBlob{};
  finalizeLastShotBlob(blob);
}

class LastShotStore {
 public:
  bool load() {
#if defined(SHOT_STOPPER_HOST_TEST)
    if (hostStorageValid_) {
      blob_ = hostStorage_;
    } else {
      resetLastShotBlob(blob_);
    }
    if (!validLastShotBlob(blob_)) {
      resetLastShotBlob(blob_);
    }
    return true;
#else
    Preferences preferences;
    if (!preferences.begin(LAST_SHOT_NAMESPACE, true)) {
      resetLastShotBlob(blob_);
      return false;
    }
    LastShotBlob candidate = {};
    const size_t length = preferences.getBytesLength(LAST_SHOT_KEY);
    bool loaded = false;
    if (length == sizeof(LastShotBlob) &&
        preferences.getBytes(LAST_SHOT_KEY, &candidate, sizeof(candidate)) ==
            sizeof(candidate) &&
        validLastShotBlob(candidate)) {
      blob_ = candidate;
      loaded = true;
    }
    preferences.end();
    if (!loaded) {
      resetLastShotBlob(blob_);
    }
    return true;
#endif
  }

  bool save() {
    finalizeLastShotBlob(blob_);
#if defined(SHOT_STOPPER_HOST_TEST)
    hostStorage_ = blob_;
    hostStorageValid_ = true;
    return true;
#else
    Preferences preferences;
    if (!preferences.begin(LAST_SHOT_NAMESPACE, false)) {
      return false;
    }
    const size_t written =
        preferences.putBytes(LAST_SHOT_KEY, &blob_, sizeof(blob_));
    preferences.end();
    return written == sizeof(blob_);
#endif
  }

  bool persist(const PersistedLastShot &shot) {
    blob_.shot = shot;
    return save();
  }

  bool clear() {
    resetLastShotBlob(blob_);
    return save();
  }

  const PersistedLastShot &get() const { return blob_.shot; }

 private:
  static constexpr const char *LAST_SHOT_NAMESPACE = "lastshot";
  static constexpr const char *LAST_SHOT_KEY = "record";

  LastShotBlob blob_;

#if defined(SHOT_STOPPER_HOST_TEST)
  static LastShotBlob hostStorage_;
  static bool hostStorageValid_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST)
LastShotBlob LastShotStore::hostStorage_;
bool LastShotStore::hostStorageValid_ = false;
#endif

}  // namespace shotstopper
