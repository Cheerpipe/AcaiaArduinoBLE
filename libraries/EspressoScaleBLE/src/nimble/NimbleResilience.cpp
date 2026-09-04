#include "nimble/NimbleResilience.h"

namespace {

constexpr uint32_t kBackoffDelaysMs[] = {100, 250, 500};
constexpr uint32_t kBackoffJitterMs = 50;

}  // namespace

void NimbleBackoffPolicy::reset() {
  deadlineMs_ = 0;
  failureCount_ = 0;
  armed_ = false;
}

uint32_t NimbleBackoffPolicy::nextRandom(uint32_t entropy) {
  randomState_ ^= entropy + 0x9e3779b9U;
  randomState_ ^= randomState_ << 13;
  randomState_ ^= randomState_ >> 17;
  randomState_ ^= randomState_ << 5;
  return randomState_;
}

uint32_t NimbleBackoffPolicy::schedule(uint32_t nowMs, uint32_t entropy) {
  const size_t index = failureCount_ < 3 ? failureCount_ : 2;
  if (failureCount_ != 0xff) {
    ++failureCount_;
  }
  const uint32_t jitter = nextRandom(entropy) % (kBackoffJitterMs + 1U);
  const uint32_t delayMs = kBackoffDelaysMs[index] + jitter;
  deadlineMs_ = nowMs + delayMs;
  armed_ = true;
  return delayMs;
}

bool NimbleBackoffPolicy::active(uint32_t nowMs) const {
  return armed_ && !nimbleTimeReached(nowMs, deadlineMs_);
}

uint32_t NimbleBackoffPolicy::remainingMs(uint32_t nowMs) const {
  return active(nowMs) ? static_cast<uint32_t>(deadlineMs_ - nowMs) : 0;
}

void NimbleNegativeCache::expire(uint32_t nowMs) {
  for (size_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    if (entries_[index].used &&
        nimbleTimeReached(nowMs, entries_[index].expiresAtMs)) {
      entries_[index].used = false;
    }
  }
}

bool NimbleNegativeCache::contains(const NimblePeerKey &key,
                                   uint32_t nowMs) {
  expire(nowMs);
  for (size_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    if (entries_[index].used && nimblePeerKeyEqual(entries_[index].key, key)) {
      entries_[index].sequence = ++sequence_;
      return true;
    }
  }
  return false;
}

void NimbleNegativeCache::insert(const NimblePeerKey &key, uint32_t nowMs,
                                 uint32_t cooldownMs) {
  expire(nowMs);
  Entry *replacement = &entries_[0];
  for (size_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    Entry &entry = entries_[index];
    if (entry.used && nimblePeerKeyEqual(entry.key, key)) {
      replacement = &entry;
      break;
    }
    if (!entry.used) {
      replacement = &entry;
      break;
    }
    if (entry.sequence < replacement->sequence) {
      replacement = &entry;
    }
  }
  replacement->key = key;
  replacement->expiresAtMs = nowMs + cooldownMs;
  replacement->sequence = ++sequence_;
  replacement->used = true;
}

void NimbleNegativeCache::erase(const NimblePeerKey &key) {
  for (size_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    if (entries_[index].used &&
        nimblePeerKeyEqual(entries_[index].key, key)) {
      entries_[index].used = false;
    }
  }
}

void NimbleNegativeCache::clear() {
  memset(entries_, 0, sizeof(entries_));
  sequence_ = 0;
}

size_t NimbleNegativeCache::activeCount(uint32_t nowMs) const {
  size_t count = 0;
  for (size_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    if (entries_[index].used &&
        !nimbleTimeReached(nowMs, entries_[index].expiresAtMs)) {
      ++count;
    }
  }
  return count;
}
