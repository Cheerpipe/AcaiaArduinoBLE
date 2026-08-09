#pragma once

#include <stdint.h>

#ifndef SHOT_STOPPER_HOST_TEST
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if !CONFIG_ESP_TASK_WDT_EN
#error "ShotStopper requires CONFIG_ESP_TASK_WDT_EN"
#endif
#if !CONFIG_ESP_TASK_WDT_PANIC
#error "ShotStopper requires CONFIG_ESP_TASK_WDT_PANIC for automatic reset"
#endif
#if !CONFIG_ESP_INT_WDT
#error "ShotStopper requires CONFIG_ESP_INT_WDT"
#endif
#if !CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT && \
    !CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT
#error "ShotStopper production builds require panic to reboot"
#endif
#endif

namespace shotstopper {

constexpr uint32_t TASK_WATCHDOG_TIMEOUT_MS = 5000;

inline bool configureTaskWatchdog() {
#ifdef SHOT_STOPPER_HOST_TEST
  hostTaskWatchdogConfigured = hostTaskWatchdogOperationsSucceed;
  return hostTaskWatchdogConfigured;
#else
  esp_task_wdt_config_t config = {};
  config.timeout_ms = TASK_WATCHDOG_TIMEOUT_MS;
  config.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
  config.trigger_panic = true;

  esp_err_t result = esp_task_wdt_reconfigure(&config);
  if (result == ESP_ERR_INVALID_STATE) {
    result = esp_task_wdt_init(&config);
  }
  return result == ESP_OK;
#endif
}

inline bool subscribeCurrentTaskToWatchdog() {
#ifdef SHOT_STOPPER_HOST_TEST
  if (!hostTaskWatchdogConfigured || !hostTaskWatchdogOperationsSucceed) {
    return false;
  }
  ++hostTaskWatchdogSubscriptions;
  return true;
#else
  const esp_err_t status = esp_task_wdt_status(nullptr);
  return status == ESP_OK || esp_task_wdt_add(nullptr) == ESP_OK;
#endif
}

inline bool feedCurrentTaskWatchdog() {
#ifdef SHOT_STOPPER_HOST_TEST
  if (!hostTaskWatchdogConfigured || !hostTaskWatchdogOperationsSucceed) {
    return false;
  }
  ++hostTaskWatchdogFeeds;
  return true;
#else
  return esp_task_wdt_reset() == ESP_OK;
#endif
}

}  // namespace shotstopper

