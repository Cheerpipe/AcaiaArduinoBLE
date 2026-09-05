#pragma once

// Snapshot payloads are ordinary C++ objects.  They must be protected by a
// real task-level synchronization primitive; sequence counters and memory
// barriers alone do not make concurrent struct copies race-free.

#if defined(SHOT_STOPPER_HOST_TEST)
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace shotstopper {

class TaskMutex {
 public:
  TaskMutex() {
#if !defined(SHOT_STOPPER_HOST_TEST)
    handle_ = xSemaphoreCreateMutexStatic(&storage_);
    configASSERT(handle_ != nullptr);
#endif
  }

  TaskMutex(const TaskMutex &) = delete;
  TaskMutex &operator=(const TaskMutex &) = delete;

  void lock() {
#if defined(SHOT_STOPPER_HOST_TEST)
    mutex_.lock();
#else
    const BaseType_t taken = xSemaphoreTake(handle_, portMAX_DELAY);
    configASSERT(taken == pdTRUE);
    (void)taken;
#endif
  }

  void unlock() {
#if defined(SHOT_STOPPER_HOST_TEST)
    mutex_.unlock();
#else
    const BaseType_t given = xSemaphoreGive(handle_);
    configASSERT(given == pdTRUE);
    (void)given;
#endif
  }

 private:
#if defined(SHOT_STOPPER_HOST_TEST)
  std::mutex mutex_;
#else
  StaticSemaphore_t storage_ = {};
  SemaphoreHandle_t handle_ = nullptr;
#endif
};

class TaskLockGuard {
 public:
  explicit TaskLockGuard(TaskMutex &mutex) : mutex_(mutex) { mutex_.lock(); }
  ~TaskLockGuard() {
    if (locked_) {
      mutex_.unlock();
    }
  }

  void unlock() {
    if (locked_) {
      mutex_.unlock();
      locked_ = false;
    }
  }

  TaskLockGuard(const TaskLockGuard &) = delete;
  TaskLockGuard &operator=(const TaskLockGuard &) = delete;

 private:
  TaskMutex &mutex_;
  bool locked_ = true;
};

}  // namespace shotstopper
