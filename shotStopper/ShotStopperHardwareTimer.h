#pragma once

#include <stdint.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <driver/gptimer.h>
#include <esp_attr.h>

#if !CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM
#error "ShotStopper requires the GPTimer ISR handler in IRAM"
#endif
#endif

namespace shotstopper {

// A one-shot deadline backed by an ESP32 general-purpose hardware timer.
// Its callback runs in interrupt context, independently of the Arduino loop,
// the BLE worker, Wi-Fi, and the esp_timer service task.
class IndependentSafetyTimer {
 public:
  using Callback = void (*)(void *context);

  IndependentSafetyTimer() = default;
  IndependentSafetyTimer(const IndependentSafetyTimer &) = delete;
  IndependentSafetyTimer &operator=(const IndependentSafetyTimer &) = delete;

  bool begin(Callback callback, void *context) {
    if (callback == nullptr || ready_) {
      return false;
    }
#ifdef SHOT_STOPPER_HOST_TEST
    if (!hostGptimerCreateSucceeds) {
      return false;
    }
    callback_ = callback;
    context_ = context;
    ready_ = true;
    return true;
#else
    gptimer_config_t config = {};
    config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    config.direction = GPTIMER_COUNT_UP;
    config.resolution_hz = 1000000;

    if (gptimer_new_timer(&config, &timer_) != ESP_OK) {
      return false;
    }
    callback_ = callback;
    context_ = context;
    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = &alarmCallback;
    if (gptimer_register_event_callbacks(timer_, &callbacks, this) != ESP_OK ||
        gptimer_enable(timer_) != ESP_OK) {
      gptimer_del_timer(timer_);
      timer_ = nullptr;
      callback_ = nullptr;
      context_ = nullptr;
      return false;
    }
    ready_ = true;
    return true;
#endif
  }

  bool arm(uint32_t timeoutMs) {
    if (!ready_ || timeoutMs == 0) {
      return false;
    }
    stop();
#ifdef SHOT_STOPPER_HOST_TEST
    if (!hostGptimerArmSucceeds) {
      return false;
    }
    dueAtUs_ = static_cast<uint64_t>(millis()) * 1000ULL +
               static_cast<uint64_t>(timeoutMs) * 1000ULL;
    running_ = true;
    return true;
#else
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = static_cast<uint64_t>(timeoutMs) * 1000ULL;
    // Publish RUNNING before start. If a very short alarm fires before
    // gptimer_start() returns, the ISR can clear it and this function must not
    // overwrite that completed state afterward.
    running_ = true;
    if (gptimer_set_raw_count(timer_, 0) != ESP_OK ||
        gptimer_set_alarm_action(timer_, &alarm) != ESP_OK ||
        gptimer_start(timer_) != ESP_OK) {
      running_ = false;
      return false;
    }
    return true;
#endif
  }

  void stop() {
    if (!running_) {
      return;
    }
#ifdef SHOT_STOPPER_HOST_TEST
    running_ = false;
#else
    // A simultaneous alarm can already have moved the driver to the enabled
    // state. ESP_ERR_INVALID_STATE is therefore harmless here.
    (void)gptimer_stop(timer_);
    running_ = false;
#endif
  }

  bool ready() const { return ready_; }
  bool running() const { return running_; }

#ifdef SHOT_STOPPER_HOST_TEST
  void serviceForHost() {
    if (!running_ ||
        static_cast<uint64_t>(millis()) * 1000ULL < dueAtUs_) {
      return;
    }
    running_ = false;
    callback_(context_);
  }

  void resetForHost() {
    dueAtUs_ = 0;
    callback_ = nullptr;
    context_ = nullptr;
    ready_ = false;
    running_ = false;
  }
#endif

 private:
#ifndef SHOT_STOPPER_HOST_TEST
  static bool IRAM_ATTR alarmCallback(
      gptimer_handle_t timer, const gptimer_alarm_event_data_t *event,
      void *userContext) {
    (void)timer;
    (void)event;
    auto *self = static_cast<IndependentSafetyTimer *>(userContext);
    self->running_ = false;
    self->callback_(self->context_);
    return false;
  }

  gptimer_handle_t timer_ = nullptr;
#else
  uint64_t dueAtUs_ = 0;
#endif
  Callback callback_ = nullptr;
  void *context_ = nullptr;
  volatile bool ready_ = false;
  volatile bool running_ = false;
};

}  // namespace shotstopper
