#include "ShotStopperBleRuntime.h"

#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {

constexpr EventBits_t kReadyBit = BIT0;
constexpr EventBits_t kHostStoppedBit = BIT1;
constexpr char kTag[] = "ble.runtime";

StaticEventGroup_t gEventStorage;
EventGroupHandle_t gEvents = nullptr;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
ShotStopperBleHealth gHealth = {
    ShotStopperBleRuntimeState::Stopped, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t gOwnAddressType = 0xff;
bool gPortInitialized = false;
TaskHandle_t gHostTask = nullptr;
ShotStopperBleGattRegistration gGattRegistration = nullptr;
void *gGattRegistrationContext = nullptr;

struct MemorySnapshot {
  uint32_t internalFreeBytes;
  uint32_t internalMinimumFreeBytes;
  uint32_t internalLargestBlockBytes;
  uint32_t psramFreeBytes;
  uint32_t psramMinimumFreeBytes;
  uint32_t psramLargestBlockBytes;
};

MemorySnapshot captureMemory() {
  const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(internalCaps),
          heap_caps_get_minimum_free_size(internalCaps),
          heap_caps_get_largest_free_block(internalCaps),
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
          heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)};
}

void storeMemoryLocked(const MemorySnapshot &memory) {
  gHealth.internalFreeBytes = memory.internalFreeBytes;
  gHealth.internalMinimumFreeBytes = memory.internalMinimumFreeBytes;
  gHealth.internalLargestBlockBytes = memory.internalLargestBlockBytes;
  gHealth.psramFreeBytes = memory.psramFreeBytes;
  gHealth.psramMinimumFreeBytes = memory.psramMinimumFreeBytes;
  gHealth.psramLargestBlockBytes = memory.psramLargestBlockBytes;
}

void onReset(int reason) {
  const MemorySnapshot memory = captureMemory();
  portENTER_CRITICAL(&gMux);
  gHealth.state = ShotStopperBleRuntimeState::Unsynced;
  gHealth.lastResetReason = reason;
  ++gHealth.resetCount;
  storeMemoryLocked(memory);
  portEXIT_CRITICAL(&gMux);
  if (gEvents != nullptr) {
    xEventGroupClearBits(gEvents, kReadyBit);
  }
}

void onSync() {
  uint8_t addressType = 0xff;
  const int rc = ble_hs_id_infer_auto(0, &addressType);
  const MemorySnapshot memory = captureMemory();
  portENTER_CRITICAL(&gMux);
  gHealth.lastError = rc;
  if (rc == 0) {
    gOwnAddressType = addressType;
    gHealth.state = ShotStopperBleRuntimeState::Ready;
    ++gHealth.syncGeneration;
    if (gHealth.syncGeneration == 0) {
      gHealth.syncGeneration = 1;
    }
  } else {
    gOwnAddressType = 0xff;
    gHealth.state = ShotStopperBleRuntimeState::Failed;
  }
  storeMemoryLocked(memory);
  portEXIT_CRITICAL(&gMux);
  if (gEvents != nullptr && rc == 0) {
    xEventGroupSetBits(gEvents, kReadyBit);
  }
}

void hostTask(void *) {
  portENTER_CRITICAL(&gMux);
  gHostTask = xTaskGetCurrentTaskHandle();
  portEXIT_CRITICAL(&gMux);
  nimble_port_run();
  const uint32_t highWater =
      static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
  portENTER_CRITICAL(&gMux);
  gHealth.hostTaskStackHighWaterWords = highWater;
  portEXIT_CRITICAL(&gMux);
  if (gEvents != nullptr) {
    xEventGroupSetBits(gEvents, kHostStoppedBit);
  }
  // The lifecycle owner deletes this task through
  // nimble_port_freertos_deinit(). Suspending here makes the join explicit and
  // avoids placing completion code after an API that deletes the current task.
  vTaskSuspend(nullptr);
}

TickType_t timeoutTicks(uint32_t timeoutMs) {
  const TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
  return ticks == 0 && timeoutMs != 0 ? 1 : ticks;
}

}  // namespace

bool shotStopperBleRuntimeConfigureGattProfile(
    ShotStopperBleGattRegistration registration, void *context) {
  portENTER_CRITICAL(&gMux);
  const bool configurable = !gPortInitialized &&
                            gHealth.state == ShotStopperBleRuntimeState::Stopped;
  if (configurable) {
    gGattRegistration = registration;
    gGattRegistrationContext = context;
  }
  portEXIT_CRITICAL(&gMux);
  return configurable;
}

bool shotStopperBleRuntimeStart(uint32_t timeoutMs) {
  if (gEvents == nullptr) {
    gEvents = xEventGroupCreateStatic(&gEventStorage);
    if (gEvents == nullptr) {
      return false;
    }
  }
  if (shotStopperBleRuntimeReady()) {
    return true;
  }

  const MemorySnapshot startingMemory = captureMemory();
  portENTER_CRITICAL(&gMux);
  const bool canStart = !gPortInitialized;
  if (canStart) {
    gHostTask = nullptr;
    gHealth.state = ShotStopperBleRuntimeState::Starting;
    gHealth.lastError = 0;
    gHealth.lastResetReason = 0;
    gHealth.hostTaskStackHighWaterWords = 0;
    storeMemoryLocked(startingMemory);
  }
  portEXIT_CRITICAL(&gMux);

  if (canStart) {
    xEventGroupClearBits(gEvents, kReadyBit | kHostStoppedBit);
    const esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
      const MemorySnapshot failedMemory = captureMemory();
      portENTER_CRITICAL(&gMux);
      gHealth.state = ShotStopperBleRuntimeState::Failed;
      gHealth.lastError = static_cast<int32_t>(err);
      storeMemoryLocked(failedMemory);
      portEXIT_CRITICAL(&gMux);
      ESP_LOGE(kTag, "nimble_port_init failed: %s", esp_err_to_name(err));
      return false;
    }
    gPortInitialized = true;
    ble_hs_cfg.reset_cb = onReset;
    ble_hs_cfg.sync_cb = onSync;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (gGattRegistration != nullptr) {
      const int registrationRc =
          gGattRegistration(gGattRegistrationContext);
      if (registrationRc != 0) {
        const MemorySnapshot failedMemory = captureMemory();
        const esp_err_t deinitError = nimble_port_deinit();
        portENTER_CRITICAL(&gMux);
        gPortInitialized = false;
        gHealth.state = ShotStopperBleRuntimeState::Failed;
        gHealth.lastError = registrationRc;
        if (deinitError != ESP_OK && gHealth.lastError == 0) {
          gHealth.lastError = static_cast<int32_t>(deinitError);
        }
        storeMemoryLocked(failedMemory);
        portEXIT_CRITICAL(&gMux);
        ESP_LOGE(kTag, "GATT profile registration failed: %d",
                 registrationRc);
        return false;
      }
    }
    nimble_port_freertos_init(hostTask);
  }

  const EventBits_t bits = xEventGroupWaitBits(
      gEvents, kReadyBit, pdFALSE, pdTRUE, timeoutTicks(timeoutMs));
  if ((bits & kReadyBit) != 0) {
    return true;
  }
  const MemorySnapshot timeoutMemory = captureMemory();
  portENTER_CRITICAL(&gMux);
  if (gHealth.state == ShotStopperBleRuntimeState::Starting) {
    gHealth.state = ShotStopperBleRuntimeState::Failed;
    gHealth.lastError = BLE_HS_ETIMEOUT;
  }
  storeMemoryLocked(timeoutMemory);
  portEXIT_CRITICAL(&gMux);
  return false;
}

bool shotStopperBleRuntimeReady() {
  portENTER_CRITICAL(&gMux);
  const bool ready = gHealth.state == ShotStopperBleRuntimeState::Ready;
  portEXIT_CRITICAL(&gMux);
  return ready;
}

uint8_t shotStopperBleRuntimeOwnAddressType() {
  portENTER_CRITICAL(&gMux);
  const uint8_t type = gOwnAddressType;
  portEXIT_CRITICAL(&gMux);
  return type;
}

uint32_t shotStopperBleRuntimeSyncGeneration() {
  portENTER_CRITICAL(&gMux);
  const uint32_t generation = gHealth.syncGeneration;
  portEXIT_CRITICAL(&gMux);
  return generation;
}

ShotStopperBleHealth shotStopperBleRuntimeHealth() {
  const MemorySnapshot memory = captureMemory();
  portENTER_CRITICAL(&gMux);
  TaskHandle_t hostTaskHandle = gHostTask;
  portEXIT_CRITICAL(&gMux);
  const uint32_t liveHighWater = hostTaskHandle == nullptr
                                     ? 0
                                     : static_cast<uint32_t>(
                                           uxTaskGetStackHighWaterMark(
                                               hostTaskHandle));
  portENTER_CRITICAL(&gMux);
  storeMemoryLocked(memory);
  if (hostTaskHandle != nullptr) {
    gHealth.hostTaskStackHighWaterWords = liveHighWater;
  }
  const ShotStopperBleHealth health = gHealth;
  portEXIT_CRITICAL(&gMux);
  return health;
}

bool shotStopperBleRuntimeStop(uint32_t timeoutMs) {
  if (!gPortInitialized || gEvents == nullptr) {
    return true;
  }
  portENTER_CRITICAL(&gMux);
  gHealth.state = ShotStopperBleRuntimeState::Stopping;
  portEXIT_CRITICAL(&gMux);
  xEventGroupClearBits(gEvents, kReadyBit | kHostStoppedBit);
  const int rc = nimble_port_stop();
  if (rc != 0) {
    portENTER_CRITICAL(&gMux);
    gHealth.state = ShotStopperBleRuntimeState::Failed;
    gHealth.lastError = rc;
    portEXIT_CRITICAL(&gMux);
    return false;
  }
  const EventBits_t bits = xEventGroupWaitBits(
      gEvents, kHostStoppedBit, pdFALSE, pdTRUE, timeoutTicks(timeoutMs));
  if ((bits & kHostStoppedBit) == 0) {
    return false;
  }
  nimble_port_freertos_deinit();
  portENTER_CRITICAL(&gMux);
  gHostTask = nullptr;
  portEXIT_CRITICAL(&gMux);
  const esp_err_t deinitError = nimble_port_deinit();
  const MemorySnapshot stoppedMemory = captureMemory();
  portENTER_CRITICAL(&gMux);
  gHealth.lastError = static_cast<int32_t>(deinitError);
  if (deinitError == ESP_OK) {
    gPortInitialized = false;
    gOwnAddressType = 0xff;
    gHealth.state = ShotStopperBleRuntimeState::Stopped;
  } else {
    gHealth.state = ShotStopperBleRuntimeState::Failed;
  }
  storeMemoryLocked(stoppedMemory);
  portEXIT_CRITICAL(&gMux);
  return deinitError == ESP_OK;
}
