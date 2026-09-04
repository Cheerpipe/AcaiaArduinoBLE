#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr size_t NIMBLE_NEGATIVE_CACHE_CAPACITY = 8;

struct NimblePeerKey {
  uint8_t address[6];
  uint8_t type;
};

inline bool nimblePeerKeyEqual(const NimblePeerKey &left,
                               const NimblePeerKey &right) {
  return left.type == right.type &&
         memcmp(left.address, right.address, sizeof(left.address)) == 0;
}

inline bool nimbleTimeReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

class NimbleBackoffPolicy {
 public:
  void reset();
  uint32_t schedule(uint32_t nowMs, uint32_t entropy);
  bool active(uint32_t nowMs) const;
  uint32_t remainingMs(uint32_t nowMs) const;
  uint8_t failureCount() const { return failureCount_; }
  uint32_t deadlineMs() const { return deadlineMs_; }

 private:
  uint32_t nextRandom(uint32_t entropy);

  uint32_t deadlineMs_ = 0;
  uint32_t randomState_ = 0x6d2b79f5U;
  uint8_t failureCount_ = 0;
  bool armed_ = false;
};

class NimbleNegativeCache {
 public:
  bool contains(const NimblePeerKey &key, uint32_t nowMs);
  void insert(const NimblePeerKey &key, uint32_t nowMs,
              uint32_t cooldownMs);
  void erase(const NimblePeerKey &key);
  void clear();
  size_t activeCount(uint32_t nowMs) const;

 private:
  struct Entry {
    NimblePeerKey key;
    uint32_t expiresAtMs;
    uint32_t sequence;
    bool used;
  };

  void expire(uint32_t nowMs);

  Entry entries_[NIMBLE_NEGATIVE_CACHE_CAPACITY] = {};
  uint32_t sequence_ = 0;
};

// Locking is intentionally external: the ESP adapter holds its short critical
// section while using this fixed ring, and host tests can exercise it without
// FreeRTOS stubs. Push never overwrites unread data.
template <typename T, size_t Capacity>
class NimbleFixedRing {
 public:
  static_assert(Capacity > 0, "a fixed ring needs at least one slot");

  bool push(const T &value) {
    if (count_ == Capacity) {
      ++drops_;
      return false;
    }
    values_[tail_] = value;
    tail_ = (tail_ + 1U) % Capacity;
    ++count_;
    if (count_ > highWater_) {
      highWater_ = count_;
    }
    return true;
  }

  bool pop(T &value) {
    if (count_ == 0) {
      return false;
    }
    value = values_[head_];
    head_ = (head_ + 1U) % Capacity;
    --count_;
    return true;
  }

  void clear() {
    head_ = 0;
    tail_ = 0;
    count_ = 0;
  }

  size_t size() const { return count_; }
  size_t highWater() const { return highWater_; }
  uint32_t drops() const { return drops_; }

 private:
  T values_[Capacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
  size_t highWater_ = 0;
  uint32_t drops_ = 0;
};
