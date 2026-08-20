#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

constexpr size_t TASK_PROFILER_MAX_TRACKED = 48;
constexpr size_t TASK_PROFILER_MAX_ROWS = 20;
constexpr size_t TASK_PROFILER_NAME_CAPACITY = 24;
constexpr uint32_t TASK_PROFILER_SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t TASK_PROFILER_MAX_DURATION_MS = 5UL * 60UL * 1000UL;

enum class TaskProfilerState : uint8_t { NEVER, RUNNING, STOPPED, FAILED };
enum class TaskProfilerStopReason : uint8_t {
  NONE,
  MANUAL,
  TIMEOUT,
  ALLOCATION_FAILED,
  CAPTURE_FAILED
};

inline const char *taskProfilerStateName(TaskProfilerState state) {
  switch (state) {
    case TaskProfilerState::NEVER: return "never";
    case TaskProfilerState::RUNNING: return "running";
    case TaskProfilerState::STOPPED: return "stopped";
    case TaskProfilerState::FAILED: return "failed";
  }
  return "failed";
}

inline const char *taskProfilerStopReasonName(TaskProfilerStopReason reason) {
  switch (reason) {
    case TaskProfilerStopReason::NONE: return "none";
    case TaskProfilerStopReason::MANUAL: return "manual";
    case TaskProfilerStopReason::TIMEOUT: return "timeout";
    case TaskProfilerStopReason::ALLOCATION_FAILED: return "allocation_failed";
    case TaskProfilerStopReason::CAPTURE_FAILED: return "capture_failed";
  }
  return "capture_failed";
}

struct TaskProfilerRow {
  char name[TASK_PROFILER_NAME_CAPACITY] = {};
  uint32_t taskNumber = 0;
  int8_t core = -1;
  uint32_t stackMinWords = 0;
  float currentCpuPct = 0.0f;
  float averageCpuPct = 0.0f;
};

struct TaskProfilerSnapshot {
  TaskProfilerState state = TaskProfilerState::NEVER;
  TaskProfilerStopReason stopReason = TaskProfilerStopReason::NONE;
  uint32_t elapsedMs = 0;
  uint32_t remainingMs = 0;
  uint32_t sampleCount = 0;
  uint32_t lastCaptureUs = 0;
  uint32_t maxCaptureUs = 0;
  float currentTotalCpuPct = 0.0f;
  float averageTotalCpuPct = 0.0f;
  float unreportedCurrentCpuPct = 0.0f;
  float unreportedAverageCpuPct = 0.0f;
  bool truncated = false;
  uint8_t rowCount = 0;
  TaskProfilerRow rows[TASK_PROFILER_MAX_ROWS] = {};
};

class TaskProfiler {
 public:
  ~TaskProfiler() { releaseWorkspace_(); }

  bool start(uint32_t nowMs) {
    if (report_.state == TaskProfilerState::RUNNING) {
      return false;
    }
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    ActiveWorkspace *next = static_cast<ActiveWorkspace *>(
        calloc(1, sizeof(ActiveWorkspace)));
    if (next == nullptr) {
      noteStartFailure_(TaskProfilerStopReason::ALLOCATION_FAILED);
      return false;
    }
    uint32_t ignoredTotal = 0;
    const int64_t captureStartedUs = esp_timer_get_time();
    const UBaseType_t count = uxTaskGetSystemState(
        next->capture, TASK_PROFILER_MAX_TRACKED, &ignoredTotal);
    const int64_t captureEndedUs = esp_timer_get_time();
    if (count == 0 || uxTaskGetNumberOfTasks() > TASK_PROFILER_MAX_TRACKED) {
      free(next);
      noteStartFailure_(TaskProfilerStopReason::CAPTURE_FAILED);
      return false;
    }
    next->trackedCount = static_cast<uint8_t>(count);
    for (UBaseType_t index = 0; index < count; ++index) {
      seedTracked_(next->tracked[index], next->capture[index]);
    }
    releaseWorkspace_();
    workspace_ = next;
    report_ = TaskProfilerSnapshot{};
    report_.state = TaskProfilerState::RUNNING;
    startedAtMs_ = nowMs;
    startedAtUs_ = captureEndedUs;
    lastCaptureAtMs_ = nowMs;
    lastCaptureAtUs_ = captureEndedUs;
    const uint64_t initialCost =
        captureEndedUs > captureStartedUs ? captureEndedUs - captureStartedUs : 0;
    report_.lastCaptureUs = clampU32_(initialCost);
    report_.maxCaptureUs = report_.lastCaptureUs;
    refreshReport_(nowMs);
    return true;
#else
    (void)nowMs;
    report_.state = TaskProfilerState::FAILED;
    report_.stopReason = TaskProfilerStopReason::CAPTURE_FAILED;
    return false;
#endif
  }

  bool stop(uint32_t nowMs) {
    if (report_.state != TaskProfilerState::RUNNING) {
      return false;
    }
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    if (static_cast<uint32_t>(nowMs - lastCaptureAtMs_) >= 100U) {
      sample_(nowMs);
    }
    if (report_.state == TaskProfilerState::RUNNING) {
      finish_(TaskProfilerState::STOPPED, TaskProfilerStopReason::MANUAL,
              nowMs);
    }
#else
    (void)nowMs;
#endif
    return true;
  }

  void service(uint32_t nowMs) {
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
    if (report_.state != TaskProfilerState::RUNNING) {
      return;
    }
    if (static_cast<uint32_t>(nowMs - startedAtMs_) >=
        TASK_PROFILER_MAX_DURATION_MS) {
      sample_(nowMs);
      if (report_.state == TaskProfilerState::RUNNING) {
        finish_(TaskProfilerState::STOPPED, TaskProfilerStopReason::TIMEOUT,
                nowMs);
      }
      return;
    }
    if (static_cast<uint32_t>(nowMs - lastCaptureAtMs_) >=
        TASK_PROFILER_SAMPLE_INTERVAL_MS) {
      sample_(nowMs);
    }
#else
    (void)nowMs;
#endif
  }

  bool running() const { return report_.state == TaskProfilerState::RUNNING; }
  const TaskProfilerSnapshot &snapshot() const { return report_; }

 private:
#if defined(ARDUINO) && !defined(SHOT_STOPPER_HOST_TEST)
  struct TrackedTask {
    char name[TASK_PROFILER_NAME_CAPACITY] = {};
    uint32_t taskNumber = 0;
    uint32_t previousCounter = 0;
    uint64_t accumulatedCounter = 0;
    uint32_t currentDelta = 0;
    uint32_t stackMinWords = 0;
    int8_t core = -1;
    bool idle = false;
  };

  struct ActiveWorkspace {
    TaskStatus_t capture[TASK_PROFILER_MAX_TRACKED] = {};
    TrackedTask tracked[TASK_PROFILER_MAX_TRACKED] = {};
    uint8_t trackedCount = 0;
  };

  static uint32_t clampU32_(uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
  }

  void noteStartFailure_(TaskProfilerStopReason reason) {
    if (report_.state == TaskProfilerState::NEVER) {
      report_.state = TaskProfilerState::FAILED;
      report_.stopReason = reason;
    }
  }

  static bool idleTaskName_(const char *name) {
    return name != nullptr &&
           (strcmp(name, "IDLE0") == 0 || strcmp(name, "IDLE1") == 0 ||
            strcmp(name, "IDLE") == 0);
  }

  static int8_t coreFor_(BaseType_t core) {
    return core == 0 || core == 1 ? static_cast<int8_t>(core) : -1;
  }

  static void seedTracked_(TrackedTask &tracked, const TaskStatus_t &task) {
    tracked = TrackedTask{};
    tracked.taskNumber = static_cast<uint32_t>(task.xTaskNumber);
    tracked.previousCounter = static_cast<uint32_t>(task.ulRunTimeCounter);
    tracked.stackMinWords = static_cast<uint32_t>(task.usStackHighWaterMark);
    tracked.core = coreFor_(task.xCoreID);
    if (task.pcTaskName != nullptr) {
      strncpy(tracked.name, task.pcTaskName, sizeof(tracked.name) - 1);
    }
    tracked.idle = idleTaskName_(tracked.name);
  }

  TrackedTask *findTracked_(uint32_t taskNumber) {
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      if (workspace_->tracked[index].taskNumber == taskNumber) {
        return &workspace_->tracked[index];
      }
    }
    return nullptr;
  }

  void sample_(uint32_t nowMs) {
    if (workspace_ == nullptr) {
      finish_(TaskProfilerState::FAILED,
              TaskProfilerStopReason::CAPTURE_FAILED, nowMs);
      return;
    }
    uint32_t ignoredTotal = 0;
    const int64_t captureStartedUs = esp_timer_get_time();
    const UBaseType_t count = uxTaskGetSystemState(
        workspace_->capture, TASK_PROFILER_MAX_TRACKED, &ignoredTotal);
    const int64_t captureEndedUs = esp_timer_get_time();
    if (count == 0) {
      finish_(TaskProfilerState::FAILED,
              TaskProfilerStopReason::CAPTURE_FAILED, nowMs);
      return;
    }
    const uint64_t intervalUs = captureEndedUs > lastCaptureAtUs_
                                    ? captureEndedUs - lastCaptureAtUs_
                                    : 0;
    if (intervalUs == 0) {
      return;
    }
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      workspace_->tracked[index].currentDelta = 0;
    }
    bool truncated = uxTaskGetNumberOfTasks() > TASK_PROFILER_MAX_TRACKED;
    for (UBaseType_t index = 0; index < count; ++index) {
      const TaskStatus_t &task = workspace_->capture[index];
      const uint32_t taskNumber = static_cast<uint32_t>(task.xTaskNumber);
      TrackedTask *tracked = findTracked_(taskNumber);
      if (tracked == nullptr) {
        if (workspace_->trackedCount >= TASK_PROFILER_MAX_TRACKED) {
          truncated = true;
          continue;
        }
        tracked = &workspace_->tracked[workspace_->trackedCount++];
        seedTracked_(*tracked, task);
        continue;
      }
      const uint32_t counter = static_cast<uint32_t>(task.ulRunTimeCounter);
      const uint32_t delta = counter - tracked->previousCounter;
      tracked->previousCounter = counter;
      tracked->currentDelta = delta;
      tracked->accumulatedCounter += delta;
      const uint32_t stackMin =
          static_cast<uint32_t>(task.usStackHighWaterMark);
      if (tracked->stackMinWords == 0 || stackMin < tracked->stackMinWords) {
        tracked->stackMinWords = stackMin;
      }
      tracked->core = coreFor_(task.xCoreID);
    }
    intervalUs_ = intervalUs;
    lastCaptureAtUs_ = captureEndedUs;
    lastCaptureAtMs_ = nowMs;
    ++report_.sampleCount;
    report_.truncated = report_.truncated || truncated;
    report_.lastCaptureUs = clampU32_(
        captureEndedUs > captureStartedUs ? captureEndedUs - captureStartedUs
                                          : 0);
    if (report_.lastCaptureUs > report_.maxCaptureUs) {
      report_.maxCaptureUs = report_.lastCaptureUs;
    }
    refreshReport_(nowMs);
  }

  void refreshReport_(uint32_t nowMs) {
    if (workspace_ == nullptr) {
      return;
    }
    report_.elapsedMs = static_cast<uint32_t>(nowMs - startedAtMs_);
    report_.remainingMs =
        report_.elapsedMs >= TASK_PROFILER_MAX_DURATION_MS
            ? 0U
            : TASK_PROFILER_MAX_DURATION_MS - report_.elapsedMs;
    report_.currentTotalCpuPct = 0.0f;
    report_.averageTotalCpuPct = 0.0f;
    report_.unreportedCurrentCpuPct = 0.0f;
    report_.unreportedAverageCpuPct = 0.0f;
    report_.rowCount = 0;
    memset(report_.rows, 0, sizeof(report_.rows));

    const uint64_t elapsedUs = lastCaptureAtUs_ > startedAtUs_
                                   ? lastCaptureAtUs_ - startedAtUs_
                                   : 0;
    for (uint8_t index = 0; index < workspace_->trackedCount; ++index) {
      const TrackedTask &tracked = workspace_->tracked[index];
      if (tracked.idle) {
        continue;
      }
      TaskProfilerRow row;
      strncpy(row.name, tracked.name, sizeof(row.name) - 1);
      row.taskNumber = tracked.taskNumber;
      row.core = tracked.core;
      row.stackMinWords = tracked.stackMinWords;
      row.currentCpuPct = intervalUs_ == 0
                              ? 0.0f
                              : static_cast<float>(tracked.currentDelta) *
                                    100.0f / static_cast<float>(intervalUs_);
      row.averageCpuPct = elapsedUs == 0
                              ? 0.0f
                              : static_cast<float>(tracked.accumulatedCounter) *
                                    100.0f / static_cast<float>(elapsedUs);
      report_.currentTotalCpuPct += row.currentCpuPct;
      report_.averageTotalCpuPct += row.averageCpuPct;

      const float rank = report_.state == TaskProfilerState::RUNNING
                             ? row.currentCpuPct
                             : row.averageCpuPct;
      size_t insertAt = report_.rowCount;
      while (insertAt > 0) {
        const TaskProfilerRow &previous = report_.rows[insertAt - 1];
        const float previousRank =
            report_.state == TaskProfilerState::RUNNING
                ? previous.currentCpuPct
                : previous.averageCpuPct;
        if (previousRank >= rank) {
          break;
        }
        --insertAt;
      }
      if (insertAt >= TASK_PROFILER_MAX_ROWS) {
        continue;
      }
      const size_t moveEnd = report_.rowCount < TASK_PROFILER_MAX_ROWS
                                 ? report_.rowCount
                                 : TASK_PROFILER_MAX_ROWS - 1;
      for (size_t move = moveEnd; move > insertAt; --move) {
        report_.rows[move] = report_.rows[move - 1];
      }
      report_.rows[insertAt] = row;
      if (report_.rowCount < TASK_PROFILER_MAX_ROWS) {
        ++report_.rowCount;
      }
    }
    float shownCurrent = 0.0f;
    float shownAverage = 0.0f;
    for (uint8_t index = 0; index < report_.rowCount; ++index) {
      shownCurrent += report_.rows[index].currentCpuPct;
      shownAverage += report_.rows[index].averageCpuPct;
    }
    report_.unreportedCurrentCpuPct =
        report_.currentTotalCpuPct > shownCurrent
            ? report_.currentTotalCpuPct - shownCurrent
            : 0.0f;
    report_.unreportedAverageCpuPct =
        report_.averageTotalCpuPct > shownAverage
            ? report_.averageTotalCpuPct - shownAverage
            : 0.0f;
    if (workspace_->trackedCount > TASK_PROFILER_MAX_ROWS) {
      report_.truncated = true;
    }
  }

  void finish_(TaskProfilerState state, TaskProfilerStopReason reason,
               uint32_t nowMs) {
    refreshReport_(nowMs);
    report_.state = state;
    report_.stopReason = reason;
    report_.remainingMs = 0;
    // Stopped reports are easier to read when ranked by the session average.
    if (workspace_ != nullptr) {
      refreshReport_(nowMs);
    }
    releaseWorkspace_();
  }

  void releaseWorkspace_() {
    if (workspace_ != nullptr) {
      free(workspace_);
      workspace_ = nullptr;
    }
  }

  ActiveWorkspace *workspace_ = nullptr;
  uint64_t startedAtUs_ = 0;
  uint64_t lastCaptureAtUs_ = 0;
  uint64_t intervalUs_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t lastCaptureAtMs_ = 0;
#else
  void releaseWorkspace_() {}
#endif

  TaskProfilerSnapshot report_ = {};
};

}  // namespace shotstopper
