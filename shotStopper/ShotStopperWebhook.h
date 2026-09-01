#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(SHOT_STOPPER_HOST_TEST) && \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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
  const char *authorityEnd = strchr(host, '/');
  if (authorityEnd == nullptr) authorityEnd = url + length;
  return authorityEnd > host;
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

 private:
  bool startWorker();
  static void taskEntry(void *parameter);
  void task();
  bool send(const WebhookEvent &event);
  bool buildPayload(const WebhookEvent &event, char *output, size_t capacity);

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  WebhookConfig config_ = {};
  WebhookStatus status_ = {};
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  char *payload_ = nullptr;
};

#endif

}  // namespace shotstopper
