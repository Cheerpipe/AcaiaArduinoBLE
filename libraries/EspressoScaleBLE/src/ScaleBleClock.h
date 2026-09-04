#ifndef ScaleBleClock_h
#define ScaleBleClock_h

#include <stdint.h>

typedef uint32_t (*ScaleBleNowMsFn)(void *context);

// Small injectable clock: two machine words, no virtual dispatch and no heap.
struct ScaleBleClock {
    ScaleBleNowMsFn nowMs;
    void *context;

    uint32_t now() const {
        return nowMs == 0 ? 0U : nowMs(context);
    }

    bool valid() const {
        return nowMs != 0;
    }
};

inline uint32_t scaleBleElapsedMs(uint32_t now, uint32_t startedAt) {
    return static_cast<uint32_t>(now - startedAt);
}

struct ScaleBleDeadline {
    uint32_t startedAtMs;
    uint32_t timeoutMs;
    bool armed;

    bool expired(uint32_t nowMs) const {
        return armed && scaleBleElapsedMs(nowMs, startedAtMs) >= timeoutMs;
    }
};

#endif
