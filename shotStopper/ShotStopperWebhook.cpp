#include "ShotStopperWebhook.h"
#include "ShotStopperPsram.h"

#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_client.h>
#include <esp_mac.h>

namespace shotstopper {
namespace {

constexpr size_t kWebhookQueueDepth = 4;
constexpr size_t kWebhookPayloadCapacity = 1024;
constexpr int kWebhookTimeoutMs = 1800;

const char *eventName(WebhookEventType type) {
  switch (type) {
    case WebhookEventType::BREWING: return "brew_state";
    case WebhookEventType::IDLE: return "brew_state";
    case WebhookEventType::FIRST_DROP: return "first_drop";
    case WebhookEventType::END: return "end";
    case WebhookEventType::TEST: return "test";
  }
  return "unknown";
}

void deviceId(char *output, size_t capacity) {
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    snprintf(output, capacity, "unknown");
    return;
  }
  snprintf(output, capacity, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace

bool WebhookDispatcher::begin(const WebhookConfig &config) {
  portENTER_CRITICAL(&mux_);
  config_ = config;
  configGeneration_ = 1;
  portEXIT_CRITICAL(&mux_);
  lifecycleMutex_ = xSemaphoreCreateMutex();
  if (lifecycleMutex_ == nullptr) {
    portENTER_CRITICAL(&mux_);
    ++status_.workerStartFailures;
    portEXIT_CRITICAL(&mux_);
    return !config.enabled;
  }
  return !config.enabled || startWorker();
}

bool WebhookDispatcher::startWorker() {
  if (lifecycleMutex_ == nullptr ||
      xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) != pdTRUE) return false;
  if (workerState_ == WorkerState::READY) {
    xSemaphoreGive(lifecycleMutex_);
    return true;
  }
  if (workerState_ == WorkerState::STARTING ||
      workerState_ == WorkerState::STOPPING) {
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  workerState_ = WorkerState::STARTING;
  stopAfterDrain_ = false;

  // Webhooks are optional. Their payload and queue contents must not consume
  // internal control/BLE heap; fail closed when PSRAM is unavailable.
  const size_t queueStorageBytes =
      kWebhookQueueDepth * sizeof(QueuedWebhook);
  uint8_t *queueStorage =
      static_cast<uint8_t *>(allocExternal(queueStorageBytes));
  char *payload =
      static_cast<char *>(allocExternal(kWebhookPayloadCapacity));
  QueueHandle_t queue = nullptr;
  if (queueStorage != nullptr) {
    queue = xQueueCreateStatic(kWebhookQueueDepth, sizeof(QueuedWebhook),
                               queueStorage, &queueControl_);
  }
  if (queue == nullptr || payload == nullptr) {
    if (queue != nullptr) vQueueDelete(queue);
    heapCapsFree(queueStorage);
    heapCapsFree(payload);
    portENTER_CRITICAL(&mux_);
    workerState_ = WorkerState::STOPPED;
    status_.workerReady = false;
    ++status_.workerStartFailures;
    portEXIT_CRITICAL(&mux_);
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  portENTER_CRITICAL(&mux_);
  queue_ = queue;
  queueStorage_ = queueStorage;
  payload_ = payload;
  portEXIT_CRITICAL(&mux_);
  if (xTaskCreatePinnedToCore(taskEntry, "webhook", 4096, this,
                             tskIDLE_PRIORITY, &task_, 0) != pdPASS) {
    vQueueDelete(queue);
    heapCapsFree(queueStorage);
    heapCapsFree(payload);
    portENTER_CRITICAL(&mux_);
    queue_ = nullptr;
    queueStorage_ = nullptr;
    payload_ = nullptr;
    task_ = nullptr;
    workerState_ = WorkerState::STOPPED;
    status_.workerReady = false;
    ++status_.workerStartFailures;
    portEXIT_CRITICAL(&mux_);
    xSemaphoreGive(lifecycleMutex_);
    return false;
  }
  portENTER_CRITICAL(&mux_);
  workerState_ = WorkerState::READY;
  status_.workerReady = true;
  portEXIT_CRITICAL(&mux_);
  xSemaphoreGive(lifecycleMutex_);
  return true;
}

void WebhookDispatcher::requestWorkerStop() {
  if (lifecycleMutex_ == nullptr ||
      xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) != pdTRUE) return;
  if (workerState_ == WorkerState::READY) workerState_ = WorkerState::STOPPING;
  stopAfterDrain_ = false;
  xSemaphoreGive(lifecycleMutex_);
}

void WebhookDispatcher::releaseWorkerFromTask() {
  QueueHandle_t queue = nullptr;
  uint8_t *queueStorage = nullptr;
  char *payload = nullptr;
  if (lifecycleMutex_ != nullptr &&
      xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
    queue = queue_;
    queueStorage = queueStorage_;
    payload = payload_;
    queue_ = nullptr;
    queueStorage_ = nullptr;
    payload_ = nullptr;
    task_ = nullptr;
    workerState_ = WorkerState::STOPPED;
    stopAfterDrain_ = false;
    portENTER_CRITICAL(&mux_);
    status_.workerReady = false;
    status_.sending = false;
    portEXIT_CRITICAL(&mux_);
    xSemaphoreGive(lifecycleMutex_);
  }
  if (queue != nullptr) vQueueDelete(queue);
  heapCapsFree(queueStorage);
  heapCapsFree(payload);
}

void WebhookDispatcher::setConfig(const WebhookConfig &config) {
  bool changed = false;
  portENTER_CRITICAL(&mux_);
  changed = memcmp(&config_, &config, sizeof(config)) != 0;
  if (changed) {
    config_ = config;
    ++configGeneration_;
    if (configGeneration_ == 0) configGeneration_ = 1;
  }
  portEXIT_CRITICAL(&mux_);
  if (config.enabled) (void)startWorker();
  else requestWorkerStop();
}

WebhookConfig WebhookDispatcher::config() const {
  WebhookConfig copy;
  portENTER_CRITICAL(&mux_);
  copy = config_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

WebhookStatus WebhookDispatcher::status() const {
  WebhookStatus copy;
  portENTER_CRITICAL(&mux_);
  copy = status_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

void WebhookDispatcher::setControlCritical(bool active) {
  controlCritical_.store(active, std::memory_order_release);
}

void WebhookDispatcher::setScaleConnecting(bool active) {
  scaleConnecting_.store(active, std::memory_order_release);
}

bool WebhookDispatcher::dispatchAllowed() const {
  return !controlCritical_.load(std::memory_order_acquire) &&
         !scaleConnecting_.load(std::memory_order_acquire);
}

bool WebhookDispatcher::enqueue(const WebhookEvent &event) {
  if (event.type == WebhookEventType::TEST) (void)startWorker();
  WebhookConfig live;
  QueuedWebhook queued;
  queued.event = event;
  QueueHandle_t queue = nullptr;
  WorkerState workerState = WorkerState::STOPPED;
  if (lifecycleMutex_ == nullptr ||
      xSemaphoreTake(lifecycleMutex_, 0) != pdTRUE) {
    portENTER_CRITICAL(&mux_);
    ++status_.dropped;
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  portENTER_CRITICAL(&mux_);
  live = config_;
  queued.configGeneration = configGeneration_;
  queue = queue_;
  workerState = workerState_;
  portEXIT_CRITICAL(&mux_);
  bool selected = event.type == WebhookEventType::TEST;
  if (event.type == WebhookEventType::BREWING ||
      event.type == WebhookEventType::IDLE) selected = live.brewState;
  if (event.type == WebhookEventType::FIRST_DROP) selected = live.firstDrop;
  if (event.type == WebhookEventType::END) selected = live.end;
  if (workerState != WorkerState::READY || queue == nullptr ||
      !validWebhookUrl(live.url) ||
      (event.type != WebhookEventType::TEST && (!live.enabled || !selected)) ||
      xQueueSend(queue, &queued, 0) != pdTRUE) {
    xSemaphoreGive(lifecycleMutex_);
    portENTER_CRITICAL(&mux_);
    ++status_.dropped;
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  if (event.type == WebhookEventType::TEST && !live.enabled) {
    stopAfterDrain_ = true;
  }
  xSemaphoreGive(lifecycleMutex_);
  return true;
}

void WebhookDispatcher::taskEntry(void *parameter) {
  static_cast<WebhookDispatcher *>(parameter)->task();
}

void WebhookDispatcher::task() {
  QueuedWebhook queued;
  for (;;) {
    QueueHandle_t queue = nullptr;
    WorkerState state = WorkerState::STOPPED;
    if (lifecycleMutex_ != nullptr &&
        xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
      queue = queue_;
      state = workerState_;
      xSemaphoreGive(lifecycleMutex_);
    }
    if (state == WorkerState::STOPPING || queue == nullptr) break;
    // Leave events queued while a shot/rinse or scale connection attempt owns
    // radio time. Queue operations and HTTP remain entirely off control/BLE.
    if (dispatchAllowed() &&
        xQueueReceive(queue, &queued, pdMS_TO_TICKS(50)) == pdTRUE) {
      (void)send(queued);
    } else {
      vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (lifecycleMutex_ != nullptr &&
        xSemaphoreTake(lifecycleMutex_, portMAX_DELAY) == pdTRUE) {
      if (workerState_ == WorkerState::READY && stopAfterDrain_ &&
          uxQueueMessagesWaiting(queue_) == 0) {
        workerState_ = WorkerState::STOPPING;
      }
      state = workerState_;
      xSemaphoreGive(lifecycleMutex_);
    }
    if (state == WorkerState::STOPPING) break;
  }
  releaseWorkerFromTask();
  vTaskDelete(nullptr);
}

bool WebhookDispatcher::buildPayload(const WebhookEvent &event, char *output,
                                     size_t capacity) {
  char id[18] = {};
  deviceId(id, sizeof(id));
  int written = snprintf(
      output, capacity,
      "{\"schemaVersion\":1,\"event\":\"%s\",\"deviceId\":\"%s\","
      "\"cycleId\":%lu,\"uptimeMs\":%lu,\"timestamp\":%lu,"
      "\"sentAtUptimeMs\":%lu",
      eventName(event.type), id, static_cast<unsigned long>(event.cycleId),
      static_cast<unsigned long>(event.uptimeMs),
      static_cast<unsigned long>(event.unixSec),
      static_cast<unsigned long>(millis()));
  if (written <= 0 || static_cast<size_t>(written) >= capacity) return false;
  size_t used = static_cast<size_t>(written);
  auto append = [&](const char *format, auto... args) {
    if (used >= capacity) return false;
    const int count = snprintf(output + used, capacity - used, format, args...);
    if (count <= 0 || static_cast<size_t>(count) >= capacity - used) return false;
    used += static_cast<size_t>(count);
    return true;
  };
  switch (event.type) {
    case WebhookEventType::BREWING:
    case WebhookEventType::IDLE:
      if (!append(",\"state\":\"%s\",\"durationMs\":%lu,"
                  "\"targetWeightG\":%.2f,\"presetId\":%u,"
                  "\"stopDetail\":\"%s\"",
                  event.type == WebhookEventType::BREWING ? "brewing" : "idle",
                  static_cast<unsigned long>(event.durationMs),
                  event.targetWeightG, static_cast<unsigned>(event.presetId),
                  event.stopDetail)) return false;
      break;
    case WebhookEventType::FIRST_DROP:
      if (!append(",\"firstDropMs\":%lu,\"weightG\":%.2f,"
                  "\"targetWeightG\":%.2f,\"presetId\":%u",
                  static_cast<unsigned long>(event.firstDropMs), event.weightG,
                  event.targetWeightG, static_cast<unsigned>(event.presetId))) {
        return false;
      }
      break;
    case WebhookEventType::END:
      if (!append(",\"durationMs\":%lu,\"targetWeightG\":%.2f,"
                  "\"presetId\":%u,\"shotType\":\"%s\","
                  "\"stopDetail\":\"%s\"",
                  static_cast<unsigned long>(event.durationMs),
                  event.targetWeightG, static_cast<unsigned>(event.presetId),
                  event.shotType, event.stopDetail)) return false;
      if (event.firstDropValid &&
          !append(",\"firstDropMs\":%lu",
                  static_cast<unsigned long>(event.firstDropMs))) return false;
      if (event.weightValid && !append(",\"weightG\":%.2f", event.weightG))
        return false;
      if (event.averageFlowValid &&
          !append(",\"averageFlowGps\":%.2f", event.averageFlowGps))
        return false;
      break;
    case WebhookEventType::TEST:
      break;
  }
  return append("}");
}

bool WebhookDispatcher::send(const QueuedWebhook &queued) {
  WebhookConfig live;
  uint32_t generation = 0;
  portENTER_CRITICAL(&mux_);
  live = config_;
  generation = configGeneration_;
  portEXIT_CRITICAL(&mux_);
  if (queued.configGeneration != generation) {
    portENTER_CRITICAL(&mux_);
    ++status_.dropped;
    ++status_.staleConfigDropped;
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  const WebhookEvent &event = queued.event;
  portENTER_CRITICAL(&mux_);
  status_.sending = true;
  status_.lastAttemptAtMs = millis();
  status_.lastHttpStatus = 0;
  status_.lastError = 0;
  portEXIT_CRITICAL(&mux_);

  bool ok = false;
  int statusCode = 0;
  esp_err_t error = ESP_FAIL;
  if (WiFi.status() == WL_CONNECTED && validWebhookUrl(live.url) &&
      buildPayload(event, payload_, kWebhookPayloadCapacity)) {
    esp_http_client_config_t config = {};
    config.url = live.url;
    config.timeout_ms = kWebhookTimeoutMs;
    config.disable_auto_redirect = true;
    config.user_data = this;
    config.event_handler = [](esp_http_client_event_t *event) -> esp_err_t {
      if (event == nullptr || event->user_data == nullptr ||
          event->event_id == HTTP_EVENT_ERROR ||
          event->event_id == HTTP_EVENT_DISCONNECTED) {
        return ESP_OK;
      }
      auto *dispatcher =
          static_cast<WebhookDispatcher *>(event->user_data);
      return dispatcher->dispatchAllowed() ? ESP_OK
                                            : ESP_ERR_INVALID_STATE;
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client != nullptr) {
      esp_http_client_set_method(client, HTTP_METHOD_POST);
      esp_http_client_set_header(client, "Content-Type", "application/json");
      esp_http_client_set_header(client, "User-Agent", "ShotStopper/1");
      esp_http_client_set_post_field(client, payload_, strlen(payload_));
      error = esp_http_client_perform(client);
      statusCode = esp_http_client_get_status_code(client);
      ok = error == ESP_OK && statusCode >= 200 && statusCode < 300;
      esp_http_client_cleanup(client);
    }
  }

  portENTER_CRITICAL(&mux_);
  status_.sending = false;
  status_.lastSuccess = ok;
  status_.lastHttpStatus = statusCode > 0 ? static_cast<uint16_t>(statusCode) : 0;
  status_.lastError = static_cast<int32_t>(error);
  if (ok) ++status_.sent;
  else ++status_.dropped;
  portEXIT_CRITICAL(&mux_);
  return ok;
}

}  // namespace shotstopper

#endif
