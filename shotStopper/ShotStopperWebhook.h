#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <atomic>

#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

constexpr size_t WEBHOOK_URL_CAPACITY = 192;
constexpr uint32_t WEBHOOK_START_MAX_DELAY_MS = 2000;
constexpr uint32_t WEBHOOK_START_MIN_DELAY_MS = 500;

struct WebhookConfig {
  bool enabled = false;
  bool brewState = true;
  bool firstDrop = true;
  bool end = true;
  char url[WEBHOOK_URL_CAPACITY] = {};
  // Deliver in real time by default. Queueing protects the scale link, but
  // defers all shot events until the shot ends.
  bool deferDuringShot = false;
};

inline bool validWebhookUrl(const char *url) {
  if (url == nullptr) return false;
  const size_t length = strnlen(url, WEBHOOK_URL_CAPACITY);
  if (length < 10 || length >= WEBHOOK_URL_CAPACITY ||
      strncmp(url, "http://", 7) != 0) {
    return false;
  }
  const char *host = url + 7;
  if (*host == '\0' || *host == '/' || strchr(host, '#') != nullptr ||
      strchr(host, '@') != nullptr) {
    return false;
  }
  for (const char *cursor = host; *cursor != '\0'; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    if (value <= 0x20U || *cursor == '"' || *cursor == '\\') return false;
  }
  const char *authorityEnd = strpbrk(host, "/?");
  if (authorityEnd == nullptr) authorityEnd = url + length;
  if (authorityEnd <= host) return false;

  const char *port = nullptr;
  const char *hostEnd = authorityEnd;
  if (*host == '[') {
    const char *close = static_cast<const char *>(
        memchr(host + 1, ']', static_cast<size_t>(authorityEnd - host - 1)));
    if (close == nullptr || close == host + 1) return false;
    for (const char *cursor = host + 1; cursor < close; ++cursor) {
      if (!(isxdigit(static_cast<unsigned char>(*cursor)) || *cursor == ':' ||
            *cursor == '.')) return false;
    }
    if (close + 1 < authorityEnd) {
      if (close[1] != ':') return false;
      port = close + 2;
    }
  } else {
    const char *colon = static_cast<const char *>(
        memchr(host, ':', static_cast<size_t>(authorityEnd - host)));
    if (colon != nullptr) {
      hostEnd = colon;
      port = colon + 1;
      if (memchr(port, ':', static_cast<size_t>(authorityEnd - port)) != nullptr)
        return false;
    }
    if (hostEnd <= host || *host == '.' || hostEnd[-1] == '.' ||
        *host == '-' || hostEnd[-1] == '-') return false;
    for (const char *cursor = host; cursor < hostEnd; ++cursor) {
      if (!(isalnum(static_cast<unsigned char>(*cursor)) || *cursor == '.' ||
            *cursor == '-' || *cursor == '_')) return false;
    }
  }
  if (port != nullptr) {
    if (port >= authorityEnd) return false;
    uint32_t value = 0;
    for (const char *cursor = port; cursor < authorityEnd; ++cursor) {
      if (!isdigit(static_cast<unsigned char>(*cursor))) return false;
      value = value * 10U + static_cast<uint32_t>(*cursor - '0');
      if (value > 65535U) return false;
    }
    if (value == 0U) return false;
  }
  return true;
}

inline bool validWebhookConfig(const WebhookConfig &config) {
  return config.enabled ? validWebhookUrl(config.url)
                        : config.url[0] == '\0' || validWebhookUrl(config.url);
}

enum class WebhookEventType : uint8_t {
  BREWING,
  IDLE,
  FIRST_DROP,
  END,
  TEST
};

struct WebhookEvent {
  WebhookEventType type = WebhookEventType::TEST;
  uint32_t cycleId = 0;
  uint32_t uptimeMs = 0;
  uint32_t unixSec = 0;
  uint32_t durationMs = 0;
  uint32_t firstDropMs = 0;
  float weightG = 0.0f;
  float targetWeightG = 0.0f;
  float averageFlowGps = 0.0f;
  bool weightValid = false;
  bool firstDropValid = false;
  bool averageFlowValid = false;
  uint8_t presetId = 0;
  char shotType[16] = {};
  char stopDetail[32] = {};
};

struct WebhookStatus {
  bool workerReady = false;
  bool sending = false;
  bool lastSuccess = false;
  uint16_t lastHttpStatus = 0;
  int32_t lastError = 0;
  uint32_t lastAttemptAtMs = 0;
  uint32_t sent = 0;
  uint32_t dropped = 0;
  uint32_t staleConfigDropped = 0;
  uint32_t workerStartFailures = 0;
};

#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)

class WebhookDispatcher {
 public:
  bool begin(const WebhookConfig &config);
  void setConfig(const WebhookConfig &config);
  WebhookConfig config() const;
  WebhookStatus status() const;
  bool enqueue(const WebhookEvent &event);
  // Radio-heavy delivery is deferred while control/BLE owns the machine when
  // configured; scale connection attempts are always protected.
  // Setters are lock-free so control and scale workers never wait on webhook
  // lifecycle/network locks.
  void setControlCritical(bool active);
  void setScaleConnecting(bool active);
  // Called by the low-priority network manager. Never call from control/BLE:
  // esp_http_client_cancel_request may wait while it tears down its socket.
  void serviceAbort();

 private:
  struct QueuedWebhook {
    WebhookEvent event;
    uint32_t configGeneration = 0;
  };

  enum class WorkerState : uint8_t { STOPPED, STARTING, READY, STOPPING };

  bool startWorker();
  void requestWorkerStop();
  void releaseWorkerFromTask();
  static void taskEntry(void *parameter);
  void task();
  bool send(const QueuedWebhook &queued);
  bool buildPayload(const WebhookEvent &event, char *output, size_t capacity);
  bool dispatchAllowed() const;

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  WebhookConfig config_ = {};
  WebhookStatus status_ = {};
  uint32_t configGeneration_ = 1;
  WorkerState workerState_ = WorkerState::STOPPED;
  SemaphoreHandle_t lifecycleMutex_ = nullptr;
  bool stopAfterDrain_ = false;
  QueueHandle_t queue_ = nullptr;
  StaticQueue_t queueControl_ = {};
  uint8_t *queueStorage_ = nullptr;
  TaskHandle_t task_ = nullptr;
  char *payload_ = nullptr;
  std::atomic<bool> controlCritical_{false};
  std::atomic<bool> deferDuringShot_{false};
  std::atomic<bool> scaleConnecting_{false};
  std::atomic<bool> abortRequested_{false};
  // Latched per active perform so a short critical pulse still cancels after
  // the level gate has cleared, including cancel_request's reconnect event.
  std::atomic<bool> cancelActive_{false};
  // Opaque here so the header does not expose esp_http_client internals.
  // mux_ protects publication/lifetime while the network task cancels a
  // perform owned by the webhook task.
  void *activeClient_ = nullptr;
  uint8_t activeClientUsers_ = 0;
  bool cancelInProgress_ = false;
};

#endif

}  // namespace shotstopper
