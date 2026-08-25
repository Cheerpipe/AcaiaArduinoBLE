#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperPreferences.h"

namespace shotstopper {

constexpr uint32_t LAST_SHOT_MAGIC = 0x4C534854U;  // "LSHT"
// Current last-shot schema. V1 is the baseline — no upgrade from prior layouts.
constexpr uint16_t LAST_SHOT_SCHEMA_VERSION = 1;

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

static_assert(sizeof(LastShotBlob) <= FLASH_IO_SCRATCH_BYTES,
              "LastShotBlob must fit in the internal flash I/O scratch");

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
    if (!lockFlashIo()) {
      resetLastShotBlob(blob_);
      return false;
    }
    Preferences preferences;
    if (!preferences.begin(LAST_SHOT_NAMESPACE, true)) {
      resetLastShotBlob(blob_);
      unlockFlashIo();
      return false;
    }
    LastShotBlob candidate = {};
    if (!preferences.isKey(LAST_SHOT_KEY)) {
      preferences.end();
      resetLastShotBlob(blob_);
      unlockFlashIo();
      return true;
    }
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
    unlockFlashIo();
    return true;
#endif
  }

  bool save(uint32_t lockTimeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
    finalizeLastShotBlob(blob_);
#if defined(SHOT_STOPPER_HOST_TEST)
    (void)lockTimeoutMs;
    hostStorage_ = blob_;
    hostStorageValid_ = true;
    return true;
#else
    if (!tryLockFlashIo(lockTimeoutMs)) {
      return false;
    }
    yieldFlashIo();
    feedFlashIoWatchdog();
    Preferences preferences;
    if (!preferences.begin(LAST_SHOT_NAMESPACE, false)) {
      unlockFlashIo();
      return false;
    }
    void *nvsSource = copyToFlashIoScratch(&blob_, sizeof(blob_));
    if (nvsSource == nullptr) {
      preferences.end();
      unlockFlashIo();
      return false;
    }
    const size_t written =
        preferences.putBytes(LAST_SHOT_KEY, nvsSource, sizeof(blob_));
    preferences.end();
    yieldFlashIo();
    feedFlashIoWatchdog();
    unlockFlashIo();
    return written == sizeof(blob_);
#endif
  }

  void adopt(const PersistedLastShot &shot) {
    blob_.shot = shot;
    finalizeLastShotBlob(blob_);
  }

  bool persist(const PersistedLastShot &shot) {
    adopt(shot);
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
