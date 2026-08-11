#pragma once

#include "ShotStopperDomain.h"

#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

namespace shotstopper {

enum class TimeSyncState : uint8_t {
  OFF = 0,
  SYNCING,
  SYNCED,
  FAILED,
  STALE
};

struct TimeStatusSnapshot {
  TimeSyncState state = TimeSyncState::OFF;
  uint32_t utcSec = 0;
  uint32_t lastSyncAgeMs = 0;
  uint32_t nextRetryInMs = 0;
  uint8_t consecutiveFailures = 0;
  char activeServer[NTP_SERVER_HOST_CAPACITY] = {};
};

inline const char *timeSyncStateName(TimeSyncState state) {
  switch (state) {
    case TimeSyncState::OFF: return "OFF";
    case TimeSyncState::SYNCING: return "SYNCING";
    case TimeSyncState::SYNCED: return "SYNCED";
    case TimeSyncState::FAILED: return "FAILED";
    case TimeSyncState::STALE: return "STALE";
  }
  return "UNKNOWN";
}

inline void resolveNtpServerHost(const RuntimeConfig &config,
                                 uint8_t failoverIndex,
                                 char output[NTP_SERVER_HOST_CAPACITY]) {
  const char *candidates[4] = {};
  size_t count = 0;
  if (config.ntpServerCustom[0] != '\0') {
    candidates[count++] = config.ntpServerCustom;
  }
  candidates[count++] = ntpPresetHostname(config.ntpServerPreset);
  candidates[count++] = "pool.ntp.org";
  candidates[count++] = "time.google.com";
  const char *selected = candidates[failoverIndex % count];
  strncpy(output, selected, NTP_SERVER_HOST_CAPACITY - 1);
  output[NTP_SERVER_HOST_CAPACITY - 1] = '\0';
}

inline uint32_t ntpRetryDelayMs(uint8_t /*consecutiveFailures*/) {
  return NTP_UNSYNCED_RETRY_MS;
}

class WallClock {
 public:
  void reset(TimeSyncState state = TimeSyncState::OFF) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    state_ = state;
    anchorUtcSec_ = 0;
    anchorMonotonicMs_ = 0;
    lastSyncMonotonicMs_ = 0;
    nextRetryAtMs_ = 0;
    consecutiveFailures_ = 0;
    activeServer_[0] = '\0';
    pendingSync_ = false;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
  }

  void queueSyncFromCallback(uint32_t utcSec) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    pendingUtcSec_ = utcSec;
    pendingSync_ = true;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
  }

  bool applyPendingSync(uint32_t monotonicMs) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    if (!pendingSync_ || pendingUtcSec_ < 1000000000U) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
      portEXIT_CRITICAL(&mux_);
#endif
      return false;
    }
    anchorUtcSec_ = pendingUtcSec_;
    anchorMonotonicMs_ = monotonicMs;
    lastSyncMonotonicMs_ = monotonicMs;
    state_ = TimeSyncState::SYNCED;
    consecutiveFailures_ = 0;
    nextRetryAtMs_ = 0;
    pendingSync_ = false;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
    return true;
  }

  void setSyncing(const char *server, uint32_t monotonicMs) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    state_ = TimeSyncState::SYNCING;
    if (server != nullptr) {
      strncpy(activeServer_, server, sizeof(activeServer_) - 1);
      activeServer_[sizeof(activeServer_) - 1] = '\0';
    }
    nextRetryAtMs_ = 0;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
    (void)monotonicMs;
  }

  void markFailed(uint32_t monotonicMs, uint8_t maxConsecutiveFailures) {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    state_ = TimeSyncState::FAILED;
    if (consecutiveFailures_ < maxConsecutiveFailures) {
      ++consecutiveFailures_;
    }
    const uint32_t delayMs = ntpRetryDelayMs(consecutiveFailures_);
    nextRetryAtMs_ = monotonicMs + delayMs;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
  }

  void markDisabled() {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    state_ = TimeSyncState::OFF;
    pendingSync_ = false;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
  }

  bool synced() const {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    const bool ready = state_ == TimeSyncState::SYNCED &&
                       anchorUtcSec_ >= 1000000000U;
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
    return ready;
  }

  uint32_t nowUtcSec(uint32_t monotonicMs) const {
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    uint32_t utcSec = 0;
    if (state_ == TimeSyncState::SYNCED && anchorUtcSec_ >= 1000000000U &&
        monotonicMs >= anchorMonotonicMs_) {
      utcSec = anchorUtcSec_ + (monotonicMs - anchorMonotonicMs_) / 1000U;
    }
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
    return utcSec;
  }

  TimeStatusSnapshot snapshot(uint32_t monotonicMs) const {
    TimeStatusSnapshot output = {};
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portENTER_CRITICAL(&mux_);
#endif
    output.state = state_;
    if (state_ == TimeSyncState::SYNCED && anchorUtcSec_ >= 1000000000U &&
        monotonicMs >= anchorMonotonicMs_) {
      output.utcSec =
          anchorUtcSec_ + (monotonicMs - anchorMonotonicMs_) / 1000U;
      if (lastSyncMonotonicMs_ <= monotonicMs) {
        output.lastSyncAgeMs = monotonicMs - lastSyncMonotonicMs_;
      }
      if (output.lastSyncAgeMs > NTP_STALE_AFTER_MS) {
        output.state = TimeSyncState::STALE;
      }
    }
    output.consecutiveFailures = consecutiveFailures_;
    if (nextRetryAtMs_ > monotonicMs) {
      output.nextRetryInMs = nextRetryAtMs_ - monotonicMs;
    }
    strncpy(output.activeServer, activeServer_, sizeof(output.activeServer) - 1);
    output.activeServer[sizeof(output.activeServer) - 1] = '\0';
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
    portEXIT_CRITICAL(&mux_);
#endif
    return output;
  }

 private:
#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
#endif
  TimeSyncState state_ = TimeSyncState::OFF;
  uint32_t anchorUtcSec_ = 0;
  uint32_t anchorMonotonicMs_ = 0;
  uint32_t lastSyncMonotonicMs_ = 0;
  uint32_t nextRetryAtMs_ = 0;
  uint8_t consecutiveFailures_ = 0;
  char activeServer_[NTP_SERVER_HOST_CAPACITY] = {};
  bool pendingSync_ = false;
  uint32_t pendingUtcSec_ = 0;
};

#if defined(SHOT_STOPPER_HOST_TEST) || \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
inline WallClock g_wallClock;
#else
extern WallClock g_wallClock;
#endif

}  // namespace shotstopper
