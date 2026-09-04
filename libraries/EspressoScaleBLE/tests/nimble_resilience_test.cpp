#include "nimble/NimbleResilience.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int checks = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " \
                << #condition << std::endl;                                    \
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)

NimblePeerKey peer(uint8_t suffix, uint8_t type = 0) {
  NimblePeerKey value = {{1, 2, 3, 4, 5, suffix}, type};
  return value;
}

void testBackoffAndWrap() {
  NimbleBackoffPolicy policy;
  const uint32_t first = policy.schedule(1000, 1);
  CHECK(first >= 100 && first <= 150);
  CHECK(policy.active(1000));
  CHECK(!policy.active(1000 + first));

  const uint32_t second = policy.schedule(2000, 2);
  CHECK(second >= 250 && second <= 300);
  const uint32_t third = policy.schedule(3000, 3);
  CHECK(third >= 500 && third <= 550);
  const uint32_t capped = policy.schedule(4000, 4);
  CHECK(capped >= 500 && capped <= 550);
  CHECK(policy.failureCount() == 4);
  policy.reset();
  CHECK(policy.failureCount() == 0);
  CHECK(!policy.active(0));

  const uint32_t nearWrap = 0xfffffff0U;
  const uint32_t wrappedDelay = policy.schedule(nearWrap, 9);
  CHECK(policy.active(nearWrap));
  CHECK(!policy.active(nearWrap + wrappedDelay));
}

void testNegativeCacheLruAndWrap() {
  NimbleNegativeCache cache;
  for (uint8_t index = 0; index < NIMBLE_NEGATIVE_CACHE_CAPACITY; ++index) {
    cache.insert(peer(index), 100, 1000);
  }
  CHECK(cache.activeCount(100) == NIMBLE_NEGATIVE_CACHE_CAPACITY);
  CHECK(cache.contains(peer(0), 101));
  cache.insert(peer(99), 102, 1000);
  CHECK(cache.contains(peer(0), 103));
  CHECK(!cache.contains(peer(1), 103));
  CHECK(cache.contains(peer(99), 103));
  cache.erase(peer(99));
  CHECK(!cache.contains(peer(99), 103));
  CHECK(cache.activeCount(1200) == 0);

  cache.insert(peer(7, 1), 0xfffffff0U, 40);
  CHECK(cache.contains(peer(7, 1), 0xfffffff5U));
  CHECK(!cache.contains(peer(7, 1), 0x18U));
}

void testSeparatedFixedRings() {
  NimbleFixedRing<uint32_t, 4> critical;
  NimbleFixedRing<uint32_t, 8> frames;
  for (uint32_t value = 1; value <= 4; ++value) {
    CHECK(critical.push(value));
  }
  CHECK(!critical.push(5));
  CHECK(critical.drops() == 1);
  CHECK(critical.highWater() == 4);

  for (uint32_t value = 0; value < 8; ++value) {
    CHECK(frames.push(100 + value));
  }
  CHECK(!frames.push(200));
  CHECK(frames.drops() == 1);
  uint32_t value = 0;
  CHECK(critical.pop(value) && value == 1);
  CHECK(frames.pop(value) && value == 100);
  CHECK(critical.size() == 3);
  CHECK(frames.size() == 7);
  critical.clear();
  frames.clear();
  CHECK(critical.size() == 0);
  CHECK(frames.size() == 0);
  CHECK(critical.highWater() == 4);
}

void testThousandRecoveryCycles() {
  NimbleBackoffPolicy backoff;
  NimbleNegativeCache cache;
  NimbleFixedRing<uint32_t, 4> critical;
  NimbleFixedRing<uint32_t, 16> frames;
  uint32_t now = 0xfffff000U;
  uint32_t generation = 0;
  uint32_t cleanupEdges = 0;
  for (uint32_t cycle = 0; cycle < 1000; ++cycle) {
    ++generation;
    CHECK(generation != 0);
    CHECK(critical.push(generation));
    for (uint32_t frame = 0; frame < 12; ++frame) {
      CHECK(frames.push((generation << 8) | frame));
    }
    uint32_t event = 0;
    CHECK(critical.pop(event));
    CHECK(event == generation);
    while (frames.pop(event)) {
      CHECK((event >> 8) == generation);
    }
    ++cleanupEdges;
    const uint32_t wait = backoff.schedule(now, generation);
    CHECK(wait >= 100 && wait <= 550);
    now += wait;
    CHECK(!backoff.active(now));
    const NimblePeerKey key = peer(static_cast<uint8_t>(cycle));
    cache.insert(key, now, 30);
    CHECK(cache.contains(key, now));
    now += 31;
    CHECK(!cache.contains(key, now));
    backoff.reset();
  }
  CHECK(cleanupEdges == 1000);
  CHECK(critical.drops() == 0);
  CHECK(frames.drops() == 0);
  CHECK(critical.size() == 0);
  CHECK(frames.size() == 0);
}

}  // namespace

int main() {
  testBackoffAndWrap();
  testNegativeCacheLruAndWrap();
  testSeparatedFixedRings();
  testThousandRecoveryCycles();
  std::cout << "NimBLE resilience tests passed: " << checks << " checks\n";
  return 0;
}
