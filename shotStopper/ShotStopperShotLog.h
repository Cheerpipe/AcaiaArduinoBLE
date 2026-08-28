#pragma once

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperPreferences.h"
#include "ShotStopperShotLogMigrate.h"
#include "ShotStopperShotLogTypes.h"

namespace shotstopper {

// Full-store scratch for load/compact. Reuses the shared flash I/O
// buffer (see ShotStopperFlashIoScratch.h). Caller must hold lockFlashIo().
inline ShotLogStore &shotLogScratchStore() {
  static_assert(sizeof(ShotLogStore) <= FLASH_IO_SCRATCH_BYTES,
                "ShotLogStore exceeds shared flash I/O scratch");
  return *reinterpret_cast<ShotLogStore *>(flashIoScratchBytes());
}

// Pack the ring into records[0..count) (oldest first) so NVS can store only
// the used prefix instead of the full capacity array.
// Caller must already hold lockFlashIo() (recursive).
inline void compactShotLogStore(ShotLogStore &store) {
  const uint16_t count = store.header.count;
  if (count == 0) {
    store.header.writeIndex = 0;
    memset(store.records, 0, sizeof(store.records));
    return;
  }

  ShotLogStore &scratch = shotLogScratchStore();
  size_t index = store.header.writeIndex;
  for (uint16_t step = 0; step < count; ++step) {
    if (index == 0) {
      index = SHOT_LOG_CAPACITY;
    }
    --index;
  }
  for (uint16_t i = 0; i < count; ++i) {
    scratch.records[i] = store.records[index];
    index = (index + 1U) % SHOT_LOG_CAPACITY;
  }
  memset(store.records, 0, sizeof(store.records));
  memcpy(store.records, scratch.records,
         static_cast<size_t>(count) * sizeof(ShotLogRecord));
  store.header.writeIndex =
      static_cast<uint16_t>(count % SHOT_LOG_CAPACITY);
}

class ShotLog {
 public:
  bool load() {
#if defined(SHOT_STOPPER_HOST_TEST)
    resetShotLogStore(store_, 1);
    if (hostStorageValid_) {
      memcpy(&store_, &hostStorage_, sizeof(store_));
    }
    const ShotLogDecodeStatus status =
        decodeShotLogBlob(&store_, sizeof(store_), store_);
    if (status == ShotLogDecodeStatus::INVALID) {
      resetShotLogStore(store_, store_.header.bootId);
    } else if (status == ShotLogDecodeStatus::MIGRATED) {
      hostStorageValid_ = true;
      memcpy(&hostStorage_, &store_, sizeof(store_));
    }
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      resetShotLogStore(store_, 1);
      dirty_ = false;
      return false;
    }
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, true)) {
      resetShotLogStore(store_, 1);
      dirty_ = false;
      unlockFlashIo();
      return false;
    }
    bool loaded = false;
    bool needsRewrite = false;
    uint8_t activeSlot = 0;
    const bool haveActive =
        preferences.isKey(SHOT_LOG_ACTIVE_KEY) &&
        preferences.getBytesLength(SHOT_LOG_ACTIVE_KEY) == 1 &&
        preferences.getBytes(SHOT_LOG_ACTIVE_KEY, &activeSlot, 1) == 1 &&
        activeSlot <= 1;

    auto tryLoadKey = [&](const char *key) -> bool {
      if (!preferences.isKey(key)) {
        return false;
      }
      const size_t length = preferences.getBytesLength(key);
      if (length == 0 || length > sizeof(store_)) {
        return false;
      }
      ShotLogStore &candidate = shotLogScratchStore();
      memset(&candidate, 0, sizeof(candidate));
      if (preferences.getBytes(key, &candidate, sizeof(candidate)) != length) {
        return false;
      }
      const ShotLogDecodeStatus status =
          decodeShotLogBlob(&candidate, length, store_);
      if (status == ShotLogDecodeStatus::INVALID) {
        return false;
      }
      if (status == ShotLogDecodeStatus::MIGRATED ||
          length != shotLogPersistedBytes(store_)) {
        needsRewrite = true;
      }
      return true;
    };

    if (haveActive) {
      loaded = tryLoadKey(slotKey(activeSlot));
      if (!loaded) {
        loaded = tryLoadKey(slotKey(static_cast<uint8_t>(1U - activeSlot)));
        if (loaded) {
          needsRewrite = true;
        }
      }
    }
    if (!loaded) {
      loaded = tryLoadKey(SHOT_LOG_KEY_A) || tryLoadKey(SHOT_LOG_KEY_B);
    }
    if (!loaded) {
      loaded = tryLoadKey(SHOT_LOG_KEY_LEGACY);
      if (loaded) {
        needsRewrite = true;
      }
    }
    preferences.end();
    if (!loaded) {
      resetShotLogStore(store_, 1);
    } else if (needsRewrite) {
      save();
    }
    dirty_ = false;
    unlockFlashIo();
    return true;
#endif
  }

  bool save(uint32_t lockTimeoutMs = FLASH_IO_LOCK_TIMEOUT_MS) {
    if (!tryLockFlashIo(lockTimeoutMs)) {
      return false;
    }
    compactShotLogStore(store_);
    finalizeShotLogStore(store_);
#if defined(SHOT_STOPPER_HOST_TEST)
    memcpy(&hostStorage_, &store_, sizeof(store_));
    hostStorageValid_ = true;
    dirty_ = false;
    unlockFlashIo();
    return true;
#else
    yieldFlashIo();
    feedFlashIoWatchdog();
    const size_t bytes = shotLogPersistedBytes(store_);
    void *nvsSource = copyToFlashIoScratch(&store_, bytes);
    if (nvsSource == nullptr) {
      unlockFlashIo();
      return false;
    }
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, false)) {
      unlockFlashIo();
      return false;
    }
    uint8_t activeSlot = 0;
    if (preferences.isKey(SHOT_LOG_ACTIVE_KEY) &&
        preferences.getBytesLength(SHOT_LOG_ACTIVE_KEY) == 1) {
      (void)preferences.getBytes(SHOT_LOG_ACTIVE_KEY, &activeSlot, 1);
    }
    if (activeSlot > 1) {
      activeSlot = 0;
    }
    const uint8_t targetSlot = static_cast<uint8_t>(1U - activeSlot);
    const char *target = slotKey(targetSlot);
    // Write the inactive slot first so a failed put never erases the last
    // good history. Only flip the active pointer after a size-matched write.
    // Source is internal SRAM scratch: the live store_ sits in PSRAM BSS.
    const size_t written = preferences.putBytes(target, nvsSource, bytes);
    yieldFlashIo();
    feedFlashIoWatchdog();
    if (written != bytes) {
      preferences.end();
      unlockFlashIo();
      return false;
    }
    const bool pointed =
        preferences.putBytes(SHOT_LOG_ACTIVE_KEY, &targetSlot, 1) == 1;
    // Best-effort cleanup of the pre-dual-slot key after a durable write.
    if (pointed && preferences.isKey(SHOT_LOG_KEY_LEGACY)) {
      preferences.remove(SHOT_LOG_KEY_LEGACY);
    }
    preferences.end();
    unlockFlashIo();
    if (pointed) {
      dirty_ = false;
    }
    return pointed;
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

  uint32_t nextRecordId() const { return store_.header.nextRecordId; }

  bool append(const ShotLogRecord &record, bool persistNow = true) {
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
    if (!persistNow) {
      dirty_ = true;
      return true;
    }
    if (save()) {
      return true;
    }
    store_.records[previousWriteIndex] = overwritten;
    store_.header.writeIndex = previousWriteIndex;
    store_.header.count = previousCount;
    store_.header.nextRecordId = previousNextRecordId;
    return false;
  }

  bool flush(uint32_t lockTimeoutMs = FLASH_IO_CONTROL_LOCK_TIMEOUT_MS) {
    if (!dirty_) {
      return true;
    }
    return save(lockTimeoutMs);
  }

  bool dirty() const { return dirty_; }

  bool updateRating(uint32_t id, uint8_t rating) {
    if (id == 0 || rating > SHOT_LOG_RATING_MAX || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    bool found = false;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      if (store_.records[index].id == id) {
        store_.records[index].extractionGuardEnabled = shotLogPackRating(
            store_.records[index].extractionGuardEnabled, rating);
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
    if (save()) {
      return true;
    }
    load();
    return false;
  }

  bool containsId(uint32_t id) const {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    size_t index = store_.header.writeIndex;
    for (size_t n = 0; n < store_.header.count; ++n) {
      if (index == 0) {
        index = SHOT_LOG_CAPACITY;
      }
      --index;
      if (store_.records[index].id == id) {
        return true;
      }
    }
    return false;
  }

  bool removeById(uint32_t id) {
    if (id == 0 || store_.header.count == 0) {
      return false;
    }
    if (!lockFlashIo()) {
      return false;
    }
    // Compact to a linear prefix so deletion is a memmove, avoiding two
    // SHOT_LOG_CAPACITY arrays on the 8 KB loopTask stack.
    compactShotLogStore(store_);
    bool found = false;
    size_t foundIndex = 0;
    for (size_t index = 0; index < store_.header.count; ++index) {
      if (store_.records[index].id == id) {
        found = true;
        foundIndex = index;
        break;
      }
    }
    if (!found) {
      unlockFlashIo();
      return false;
    }
    const uint16_t previousCount = store_.header.count;
    if (foundIndex + 1U < previousCount) {
      memmove(&store_.records[foundIndex], &store_.records[foundIndex + 1U],
              static_cast<size_t>(previousCount - foundIndex - 1U) *
                  sizeof(ShotLogRecord));
    }
    --store_.header.count;
    store_.header.writeIndex =
        static_cast<uint16_t>(store_.header.count % SHOT_LOG_CAPACITY);
    memset(&store_.records[store_.header.count], 0, sizeof(ShotLogRecord));
    const bool saved = save();
    unlockFlashIo();
    if (saved) {
      return true;
    }
    load();
    return false;
  }

  bool erasePersisted() {
#if defined(SHOT_STOPPER_HOST_TEST)
    hostStorageValid_ = false;
    resetShotLogStore(store_,
                      store_.header.bootId == 0 ? 1U : store_.header.bootId);
    dirty_ = false;
    return true;
#else
    if (!lockFlashIo()) {
      return false;
    }
    Preferences preferences;
    if (!preferences.begin(SHOT_LOG_NAMESPACE, false)) {
      unlockFlashIo();
      return false;
    }
    (void)preferences.remove(SHOT_LOG_KEY_A);
    (void)preferences.remove(SHOT_LOG_KEY_B);
    (void)preferences.remove(SHOT_LOG_ACTIVE_KEY);
    (void)preferences.remove(SHOT_LOG_KEY_LEGACY);
    preferences.end();
    unlockFlashIo();
    resetShotLogStore(store_,
                      store_.header.bootId == 0 ? 1U : store_.header.bootId);
    dirty_ = false;
    return true;
#endif
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
  static constexpr const char *SHOT_LOG_KEY_LEGACY = "records";
  static constexpr const char *SHOT_LOG_KEY_A = "recordsA";
  static constexpr const char *SHOT_LOG_KEY_B = "recordsB";
  static constexpr const char *SHOT_LOG_ACTIVE_KEY = "active";

  static const char *slotKey(uint8_t slot) {
    return slot == 0 ? SHOT_LOG_KEY_A : SHOT_LOG_KEY_B;
  }

  ShotLogStore store_{};
  bool dirty_ = false;

#if defined(SHOT_STOPPER_HOST_TEST)
  static ShotLogStore hostStorage_;
  static bool hostStorageValid_;
#endif
};

#if defined(SHOT_STOPPER_HOST_TEST)
ShotLogStore ShotLog::hostStorage_;
bool ShotLog::hostStorageValid_ = false;
#endif

}  // namespace shotstopper
