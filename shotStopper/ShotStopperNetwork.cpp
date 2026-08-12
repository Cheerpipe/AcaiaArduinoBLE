#include "ShotStopperNetwork.h"
#include "ShotStopperWatchdog.h"

#include "ShotStopperWebAssets.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace shotstopper {

WallClock g_wallClock;

namespace {

constexpr const char *AP_SSID = "MicraShotStopperAP";
constexpr const char *AP_IP = "192.168.4.1";
constexpr const char *JSON_CONTENT_TYPE = "application/json";
constexpr const char *STATUS_OK = "200 OK";
constexpr const char *STATUS_ACCEPTED = "202 Accepted";
constexpr const char *STATUS_BAD_REQUEST = "400 Bad Request";
constexpr const char *STATUS_UNAUTHORIZED = "401 Unauthorized";
constexpr const char *STATUS_TOO_MANY = "429 Too Many Requests";
constexpr const char *STATUS_CONFLICT = "409 Conflict";
constexpr const char *STATUS_NOT_FOUND = "404 Not Found";
constexpr const char *STATUS_UNPROCESSABLE = "422 Unprocessable Entity";
constexpr const char *STATUS_UNAVAILABLE = "503 Service Unavailable";

const char *scaleDisconnectReasonName(uint8_t reason) {
  // Mirrors AcaiaDisconnectReason without coupling the network task to the
  // single-owner BLE implementation.
  switch (reason) {
    case 0: return "NONE";
    case 1: return "USER_REQUEST";
    case 2: return "SCAN_START_FAILED";
    case 3: return "SCAN_TIMEOUT";
    case 4: return "CONNECT_FAILED";
    case 5: return "DISCOVERY_FAILED";
    case 6: return "UNSUPPORTED_SCALE";
    case 7: return "SUBSCRIBE_FAILED";
    case 8: return "INITIALIZATION_WRITE_FAILED";
    case 9: return "REMOTE_DISCONNECTED";
    case 10: return "FIRST_PACKET_TIMEOUT";
    case 11: return "PACKET_TIMEOUT";
    case 12: return "INVALID_PACKET_STREAM";
    case 13: return "COMMAND_WRITE_FAILED";
  }
  return "UNKNOWN";
}

bool jsonBoolean(cJSON *object, const char *name, bool &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsBool(item)) {
    return false;
  }
  output = cJSON_IsTrue(item);
  return true;
}

bool jsonUint32(cJSON *object, const char *name, uint32_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
      item->valuedouble < 0.0 || item->valuedouble > UINT32_MAX ||
      floor(item->valuedouble) != item->valuedouble) {
    return false;
  }
  output = static_cast<uint32_t>(item->valuedouble);
  return true;
}

bool jsonUint8(cJSON *object, const char *name, uint8_t &output) {
  uint32_t value = 0;
  if (!jsonUint32(object, name, value) || value > UINT8_MAX) {
    return false;
  }
  output = static_cast<uint8_t>(value);
  return true;
}

bool jsonFloat(cJSON *object, const char *name, float &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
    return false;
  }
  output = static_cast<float>(item->valuedouble);
  return true;
}

bool jsonInt16(cJSON *object, const char *name, int16_t &output) {
  if (object == nullptr || name == nullptr) {
    return false;
  }
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint) {
    return false;
  }
  const long value = item->valueint;
  if (value < INT16_MIN || value > INT16_MAX) {
    return false;
  }
  output = static_cast<int16_t>(value);
  return true;
}

bool jsonString(cJSON *object, const char *name, char *output,
                size_t outputCapacity, bool allowEmpty) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  const size_t length = strlen(item->valuestring);
  if ((!allowEmpty && length == 0) || length >= outputCapacity) {
    return false;
  }
  memset(output, 0, outputCapacity);
  memcpy(output, item->valuestring, length);
  return true;
}

bool jsonHasOnlyUniqueFields(cJSON *object, const char *const *allowed,
                             size_t allowedCount) {
  if (!cJSON_IsObject(object) || allowed == nullptr || allowedCount > 32) {
    return false;
  }
  uint32_t seen = 0;
  for (cJSON *item = object->child; item != nullptr; item = item->next) {
    if (item->string == nullptr) {
      return false;
    }
    size_t index = 0;
    while (index < allowedCount && strcmp(item->string, allowed[index]) != 0) {
      ++index;
    }
    if (index == allowedCount || (seen & (1UL << index)) != 0) {
      return false;
    }
    seen |= 1UL << index;
  }
  return true;
}

const char *configValidationMessage(ConfigValidationError error) {
  switch (error) {
    case ConfigValidationError::NONE:
      return "Configuration is valid.";
    case ConfigValidationError::GOAL_WEIGHT:
      return "Target must be an integer from 10 to 200 g.";
    case ConfigValidationError::WEIGHT_OFFSET:
      return "Internal offset is invalid; it cannot be edited from the Web UI.";
    case ConfigValidationError::RINSE_GESTURE:
      return "Rinse gesture must be from 100 to 5,000 ms.";
    case ConfigValidationError::RINSE_DURATION:
      return "Rinse duration must be from 500 to 10,000 ms.";
    case ConfigValidationError::RETARE_WINDOW:
      return "Retare window must be from 500 to 10,000 ms.";
    case ConfigValidationError::MINIMUM_CUP_WEIGHT:
      return "Minimum cup weight must be from 1 to 500 g.";
    case ConfigValidationError::RETARE_STABILITY_SAMPLES:
      return "Retare stable samples must be from 2 to 10.";
    case ConfigValidationError::RETARE_STABILITY_TOLERANCE:
      return "Retare stability tolerance must be from 0.1 to 20.0 g.";
    case ConfigValidationError::RETARE_STABILITY_MAX_GAP:
      return "Retare sample gap must be from 100 to 5,000 ms.";
    case ConfigValidationError::RETARE_STABILITY_MIN_DURATION:
      return "Retare min stable time must be from 0 to 2,000 ms.";
    case ConfigValidationError::RETARE_STABILITY_RELATION:
      return "Retare min stable time must fit within the retare window and "
             "sample count times the sample gap.";
    case ConfigValidationError::CONFIRMATION_TIMEOUT:
      return "Brew start confirmation must be from 500 to 30,000 ms.";
    case ConfigValidationError::CONFIRMATION_RETARE_RELATION:
      return "Brew start confirmation must be at least retare window + 3 s.";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "CN9 limit must be from 5,000 to 60,000 ms.";
    case ConfigValidationError::PADDLE_REMINDER_INTERVAL:
      return "Paddle reminder interval must be from 5,000 to 60,000 ms.";
    case ConfigValidationError::PADDLE_REMINDER_MAX_DURATION:
      return "Paddle reminder limit must be from 60,000 to 3,600,000 ms and "
             "at least the reminder interval.";
    case ConfigValidationError::TIMING_RELATION:
      return "Required: rinse gesture < CN9 limit, rinse duration <= limit, "
             "retare window + brew start confirmation <= limit.";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "The Bookoo combined command requires automatic tare.";
    case ConfigValidationError::TIMEZONE_OFFSET:
      return "Timezone offset must be from -720 to +840 minutes.";
    case ConfigValidationError::NTP_SERVER_PRESET:
      return "NTP server preset must be pool, google, cloudflare, or nist.";
    case ConfigValidationError::NTP_SERVER_CUSTOM:
      return "Custom NTP hostname is invalid.";
  }
  return "Invalid configuration.";
}

bool jsonNtpPreset(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "pool") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::POOL);
    return true;
  }
  if (strcmp(item->valuestring, "google") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::GOOGLE);
    return true;
  }
  if (strcmp(item->valuestring, "cloudflare") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::CLOUDFLARE);
    return true;
  }
  if (strcmp(item->valuestring, "nist") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::NIST);
    return true;
  }
  return false;
}

const char *ntpPresetId(uint8_t preset) {
  switch (preset) {
    case static_cast<uint8_t>(NtpServerPreset::GOOGLE):
      return "google";
    case static_cast<uint8_t>(NtpServerPreset::CLOUDFLARE):
      return "cloudflare";
    case static_cast<uint8_t>(NtpServerPreset::NIST):
      return "nist";
    case static_cast<uint8_t>(NtpServerPreset::POOL):
    default:
      return "pool";
  }
}

bool constantTimeTokenEqual(const char *left, const char *right,
                            size_t capacity) {
  uint8_t difference = 0;
  for (size_t index = 0; index < capacity; ++index) {
    difference |= static_cast<uint8_t>(left[index]) ^
                  static_cast<uint8_t>(right[index]);
  }
  return difference == 0;
}

void formatIp(const IPAddress &ip, char output[16]) {
  snprintf(output, 16, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]),
           static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
           static_cast<unsigned>(ip[3]));
}

bool registerHandler(httpd_handle_t server, const char *uri,
                     httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t descriptor = {};
  descriptor.uri = uri;
  descriptor.method = method;
  descriptor.handler = handler;
  return httpd_register_uri_handler(server, &descriptor) == ESP_OK;
}

esp_err_t sendJsonStringChunk(httpd_req_t *request, const char *value) {
  if (httpd_resp_send_chunk(request, "\"", 1) != ESP_OK) {
    return ESP_FAIL;
  }
  const unsigned char *cursor =
      reinterpret_cast<const unsigned char *>(value);
  const unsigned char *segment = cursor;
  while (*cursor != '\0') {
    const char *escape = nullptr;
    char unicodeEscape[7] = {};
    switch (*cursor) {
      case '\"': escape = "\\\""; break;
      case '\\': escape = "\\\\"; break;
      case '\b': escape = "\\b"; break;
      case '\f': escape = "\\f"; break;
      case '\n': escape = "\\n"; break;
      case '\r': escape = "\\r"; break;
      case '\t': escape = "\\t"; break;
      default:
        if (*cursor < 0x20) {
          snprintf(unicodeEscape, sizeof(unicodeEscape), "\\u%04x", *cursor);
          escape = unicodeEscape;
        }
        break;
    }
    if (escape != nullptr) {
      if (cursor > segment &&
          httpd_resp_send_chunk(
              request, reinterpret_cast<const char *>(segment),
              static_cast<ssize_t>(cursor - segment)) != ESP_OK) {
        return ESP_FAIL;
      }
      if (httpd_resp_send_chunk(request, escape, HTTPD_RESP_USE_STRLEN) !=
          ESP_OK) {
        return ESP_FAIL;
      }
      segment = cursor + 1;
    }
    ++cursor;
  }
  if (cursor > segment &&
      httpd_resp_send_chunk(request, reinterpret_cast<const char *>(segment),
                            static_cast<ssize_t>(cursor - segment)) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, "\"", 1);
}

char g_statusResponseBuffer[6144];

void sanitizeJsonEmbed(const char *input, char *output, size_t capacity) {
  if (capacity == 0) {
    return;
  }
  size_t written = 0;
  for (size_t index = 0; input != nullptr && input[index] != '\0' &&
                          written + 1 < capacity;
       ++index) {
    const unsigned char byte = static_cast<unsigned char>(input[index]);
    if (byte == '\"' || byte == '\\' || byte < 0x20) {
      continue;
    }
    output[written++] = static_cast<char>(byte);
  }
  output[written] = '\0';
}

bool applySystemTimeToWallClock(uint32_t now) {
  time_t nowSec = 0;
  time(&nowSec);
  if (nowSec < 1000000000) {
    return false;
  }
  g_wallClock.queueSyncFromCallback(static_cast<uint32_t>(nowSec));
  return g_wallClock.applyPendingSync(now);
}

}  // namespace

ShotStopperNetwork *ShotStopperNetwork::instance_ = nullptr;

bool ShotStopperNetwork::begin(const PersistedSettings &settings,
                               const NetworkBridgeCallbacks &callbacks) {
  if (instance_ != nullptr || callbacks.copyControlStatus == nullptr ||
      callbacks.enqueueWebCommand == nullptr ||
      callbacks.copyDebugEvents == nullptr ||
      callbacks.reportTaskWatchdogFault == nullptr ||
      callbacks.requestSafeRestart == nullptr) {
    return false;
  }
  settings_ = settings;
  callbacks_ = callbacks;
  acceptedCommandQueue_ =
      xQueueCreate(WEB_COMMAND_QUEUE_LENGTH, sizeof(WebCommand));
  if (acceptedCommandQueue_ == nullptr) {
    return false;
  }
  instance_ = this;
  ntpConfigRevision_ = settings_.runtime.revision;
  g_wallClock.reset();
  if (xTaskCreate(taskEntry, "network_manager", 10240, this,
                  tskIDLE_PRIORITY + 1, &taskHandle_) != pdPASS) {
    instance_ = nullptr;
    vQueueDelete(acceptedCommandQueue_);
    acceptedCommandQueue_ = nullptr;
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

bool ShotStopperNetwork::enqueueAcceptedCommand(const WebCommand &command) {
  return acceptedCommandQueue_ != nullptr &&
         xQueueSend(acceptedCommandQueue_, &command, 0) == pdTRUE;
}

void ShotStopperNetwork::requestNtpSyncIfNeeded() {
  if (!wallClockNeedsActivityNtpSync(g_wallClock, millis())) {
    return;
  }
  portENTER_CRITICAL(&dataMux_);
  ntpActivitySyncPending_ = true;
  portEXIT_CRITICAL(&dataMux_);
}

NetworkStatusSnapshot ShotStopperNetwork::snapshot() {
  NetworkStatusSnapshot copy;
  portENTER_CRITICAL(&dataMux_);
  copy = status_;
  portEXIT_CRITICAL(&dataMux_);
  copy.taskAgeMs = static_cast<uint32_t>(millis() - lastTaskProgressAtMs_);
  copy.taskStackMinWords = taskStackMinWords_;
  return copy;
}

PersistedSettings ShotStopperNetwork::settingsCopy() {
  PersistedSettings copy;
  portENTER_CRITICAL(&dataMux_);
  copy = settings_;
  portEXIT_CRITICAL(&dataMux_);
  return copy;
}

uint32_t ShotStopperNetwork::allocateRequestId() {
  portENTER_CRITICAL(&dataMux_);
  const uint32_t id = nextRequestId_++;
  if (nextRequestId_ == 0 || nextRequestId_ >= 0x80000000UL) {
    nextRequestId_ = 1;
  }
  portEXIT_CRITICAL(&dataMux_);
  return id == 0 ? 1 : id;
}

uint32_t ShotStopperNetwork::allocateSessionId() {
  portENTER_CRITICAL(&dataMux_);
  const uint32_t id = nextSessionId_++;
  if (nextSessionId_ == 0) {
    nextSessionId_ = 1;
  }
  portEXIT_CRITICAL(&dataMux_);
  return id == 0 ? 1 : id;
}

uint32_t ShotStopperNetwork::allocateControlLeaseId() {
  portENTER_CRITICAL(&dataMux_);
  const uint32_t id = nextControlLeaseId_++;
  if (nextControlLeaseId_ == 0) {
    nextControlLeaseId_ = 1;
  }
  portEXIT_CRITICAL(&dataMux_);
  return id == 0 ? 1 : id;
}

void ShotStopperNetwork::recordCommandResult(
    uint32_t requestId, CommandResultState state) {
  if (requestId == 0 || requestId >= 0x80000000UL) {
    return;
  }
  portENTER_CRITICAL(&dataMux_);
  if (status_.lastCommandRequestId == 0 ||
      status_.lastCommandRequestId == requestId ||
      static_cast<int32_t>(requestId - status_.lastCommandRequestId) > 0) {
    status_.lastCommandRequestId = requestId;
    status_.lastCommandState = state;
  }
  portEXIT_CRITICAL(&dataMux_);
}

esp_err_t ShotStopperNetwork::sendAccepted(httpd_req_t *request,
                                           uint32_t requestId,
                                           const char *extraJson) {
  recordCommandResult(requestId, CommandResultState::QUEUED);
  char response[160] = {};
  if (extraJson == nullptr) {
    snprintf(response, sizeof(response),
             "{\"accepted\":true,\"requestId\":%lu}",
             static_cast<unsigned long>(requestId));
  } else {
    snprintf(response, sizeof(response),
             "{\"accepted\":true,\"requestId\":%lu,%s}",
             static_cast<unsigned long>(requestId), extraJson);
  }
  return sendJson(request, STATUS_ACCEPTED, response);
}

void ShotStopperNetwork::taskEntry(void *parameter) {
  static_cast<ShotStopperNetwork *>(parameter)->taskLoop();
}

void ShotStopperNetwork::taskLoop() {
  if (!subscribeCurrentTaskToWatchdog()) {
    callbacks_.reportTaskWatchdogFault();
  }
  uint32_t telemetryAtMs = 0;
  for (;;) {
    lastTaskProgressAtMs_ = millis();
    if (static_cast<uint32_t>(lastTaskProgressAtMs_ - telemetryAtMs) >=
        HEALTH_TELEMETRY_INTERVAL_MS) {
      telemetryAtMs = lastTaskProgressAtMs_;
      taskStackMinWords_ =
          static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    }
    service();
    if (!feedCurrentTaskWatchdog()) {
      callbacks_.reportTaskWatchdogFault();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void ShotStopperNetwork::service() {
  const uint32_t now = millis();
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);

  if (!startupComplete_ && controlAllowsConfiguration(control) &&
      static_cast<int32_t>(now - networkRetryAtMs_) >= 0) {
    if (startNetwork()) {
      startupFailures_ = 0;
      networkRetryAtMs_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      portEXIT_CRITICAL(&dataMux_);
    } else {
      if (startupFailures_ < UINT8_MAX) {
        ++startupFailures_;
      }
      const uint8_t shift = startupFailures_ > 5 ? 5 : startupFailures_;
      uint32_t backoff = NETWORK_RETRY_MIN_MS << shift;
      if (backoff > NETWORK_RETRY_MAX_MS) {
        backoff = NETWORK_RETRY_MAX_MS;
      }
      networkRetryAtMs_ = now + backoff;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = startupFailures_;
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
          startupFailures_, static_cast<int32_t>(backoff));
    }
  }
  if (!startupComplete_) {
    return;
  }

  processAcceptedCommands();
  serviceStaState(now);
  serviceWifiScan(now);
  serviceSessions(now);

  const NetworkStatusSnapshot networkSnapshot = snapshot();
  const bool staConnected =
      networkSnapshot.staState == StaState::CONNECTED &&
      networkSnapshot.wifiConfigured && WiFi.status() == WL_CONNECTED &&
      networkSnapshot.staIp[0] != '\0';
  serviceNtp(now, staConnected);

  const NetworkStatusSnapshot network = networkSnapshot;
  const uint8_t apClients = network.apActive
                                ? static_cast<uint8_t>(
                                      WiFi.softAPgetStationNum())
                                : 0;
  portENTER_CRITICAL(&dataMux_);
  status_.apClients = apClients;
  portEXIT_CRITICAL(&dataMux_);

  callbacks_.copyControlStatus(control);
  const bool safeForNetworkChange = controlAllowsConfiguration(control);
  if (apRestartPending_ && safeForNetworkChange &&
      static_cast<uint32_t>(now - restartRequestedAtMs_) >= RESTART_DELAY_MS) {
    apRestartPending_ = false;
    if (startFallbackAccessPoint(now)) {
      startupFailures_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      portEXIT_CRITICAL(&dataMux_);
    } else {
      startupComplete_ = false;
      if (startupFailures_ < UINT8_MAX) {
        ++startupFailures_;
      }
      networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = startupFailures_;
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
          startupFailures_, NETWORK_RETRY_MIN_MS);
    }
  }

  if (restartPending_ && safeForNetworkChange &&
      static_cast<uint32_t>(now - restartRequestedAtMs_) >= RESTART_DELAY_MS) {
    restartPending_ = false;
    callbacks_.requestSafeRestart();
  }

  if (networkShutdownPending_ && safeForNetworkChange) {
    stopNetwork();
  }
}

bool ShotStopperNetwork::controlAllowsNetworkMutation(
    ControlStatusSnapshot *copy) {
  ControlStatusSnapshot status;
  callbacks_.copyControlStatus(status);
  if (copy != nullptr) {
    *copy = status;
  }
  return controlAllowsConfiguration(status);
}

bool ShotStopperNetwork::startNetwork() {
  const PersistedSettings settings = settingsCopy();
  const uint32_t now = millis();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  portENTER_CRITICAL(&dataMux_);
  status_.wifiConfigured = settings.staConfigured;
  strncpy(status_.apIp, AP_IP, sizeof(status_.apIp) - 1);
  portEXIT_CRITICAL(&dataMux_);
  if (settings.staConfigured) {
    startStation(settings, now);
    startupComplete_ = true;
    return true;
  } else {
    startupComplete_ = startFallbackAccessPoint(now);
    return startupComplete_;
  }
}

void ShotStopperNetwork::startStation(const PersistedSettings &settings,
                                      uint32_t now) {
  stopHttpServer();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  staConnectStartedAtMs_ = now;
  staReconnectAttemptAtMs_ = now;
  staEverConnected_ = false;
  networkShutdownPending_ = false;
  portENTER_CRITICAL(&dataMux_);
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::STA_CONNECTING);
  Serial.println("WiFi STA connecting; AP disabled");
  WiFi.begin(settings.staSsid,
             settings.staOpen ? nullptr : settings.staPassword);
}

bool ShotStopperNetwork::startFallbackAccessPoint(uint32_t now) {
  const PersistedSettings settings = settingsCopy();
  stopHttpServer();
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP);
  const IPAddress ip(192, 168, 4, 1);
  const bool configured =
      WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool apReady = configured && WiFi.softAP(AP_SSID, settings.apPassword);
  const bool httpReady = apReady && startHttpServer();
  networkStartedAtMs_ = now;
  networkShutdownPending_ = false;
  portENTER_CRITICAL(&dataMux_);
  everAuthenticated_ = false;
  lastAuthenticatedAtMs_ = now;
  status_.networkActive = apReady && httpReady;
  status_.apActive = apReady;
  status_.uiAuthenticated = false;
  status_.apClients = 0;
  if (!settings.staConfigured) {
    status_.staState = StaState::NOT_CONFIGURED;
  }
  status_.staIp[0] = '\0';
  status_.windowRemainingMs = AP_WINDOW_MS;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::AP_STARTED, apReady, httpReady);
  Serial.print("WiFi fallback AP ");
  Serial.print(apReady && httpReady ? "ready at " : "failed at ");
  Serial.println(AP_IP);
  if (!apReady || !httpReady) {
    stopHttpServer();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  return apReady && httpReady;
}

void ShotStopperNetwork::stopNetwork() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  stopHttpServer();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  invalidateAllSessions();
  portENTER_CRITICAL(&dataMux_);
  scanRequested_ = false;
  scan_ = WifiScanSnapshot{};
  status_.networkActive = false;
  status_.apActive = false;
  status_.uiAuthenticated = false;
  status_.apClients = 0;
  status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                            : StaState::NOT_CONFIGURED;
  status_.staIp[0] = '\0';
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  networkShutdownPending_ = false;
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
}

void ShotStopperNetwork::serviceStaState(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (status.apActive || !status.wifiConfigured) {
    return;
  }

  if (status.staState == StaState::CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::DISCONNECTED;
      status_.networkActive = false;
      status_.staIp[0] = '\0';
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
          static_cast<int32_t>(WiFi.status()));
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      staReconnectAttemptAtMs_ = now;
      Serial.println("WiFi STA disconnected; Web server waiting for reconnect");
      return;
    }
    if (server_ == nullptr && controlAllowsNetworkMutation() &&
        static_cast<int32_t>(now - httpRetryAtMs_) >= 0) {
      const bool httpReady = startHttpServer();
      httpRetryAtMs_ = httpReady ? 0 : now + HTTP_RETRY_MS;
      portENTER_CRITICAL(&dataMux_);
      status_.networkActive = httpReady;
      portEXIT_CRITICAL(&dataMux_);
    }
    return;
  }

  if ((status.staState == StaState::CONNECTING ||
       status.staState == StaState::DISCONNECTED) &&
      WiFi.status() == WL_CONNECTED) {
    char address[16] = {};
    formatIp(WiFi.localIP(), address);
    const bool mayStartHttp =
        controlAllowsNetworkMutation() &&
        static_cast<int32_t>(now - httpRetryAtMs_) >= 0;
    const bool httpReady = mayStartHttp && startHttpServer();
    httpRetryAtMs_ = httpReady ? 0 : now + HTTP_RETRY_MS;
    staEverConnected_ = true;
    networkShutdownPending_ = false;
    portENTER_CRITICAL(&dataMux_);
    status_.staState = StaState::CONNECTED;
    status_.networkActive = httpReady;
    strncpy(status_.staIp, address, sizeof(status_.staIp) - 1);
    status_.windowRemainingMs = 0;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::NETWORK, DebugCode::STA_CONNECTED);
    Serial.print("WiFi STA connected; IP: ");
    Serial.println(address);
    staNtpEligibleAtMs_ = now + NTP_STA_SETTLE_MS;
    ntpRearmPending_ = true;
    return;
  }

  if (status.staState == StaState::CONNECTING && !staEverConnected_ &&
      static_cast<uint32_t>(now - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS && controlAllowsNetworkMutation()) {
    WiFi.disconnect(false, false);
    portENTER_CRITICAL(&dataMux_);
    status_.staState = StaState::FAILED;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
        static_cast<int32_t>(WiFi.status()));
    Serial.println("WiFi STA failed; starting fallback AP");
    if (!startFallbackAccessPoint(now)) {
      startupComplete_ = false;
      if (startupFailures_ < UINT8_MAX) {
        ++startupFailures_;
      }
      networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = startupFailures_;
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::NETWORK_RETRY,
          startupFailures_, NETWORK_RETRY_MIN_MS);
    } else {
      startupFailures_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      portEXIT_CRITICAL(&dataMux_);
    }
    return;
  }

  if (status.staState == StaState::DISCONNECTED && staEverConnected_ &&
      controlAllowsNetworkMutation() &&
      static_cast<uint32_t>(now - staReconnectAttemptAtMs_) >=
          STA_RECONNECT_INTERVAL_MS) {
    staReconnectAttemptAtMs_ = now;
    log(DebugCategory::NETWORK, DebugCode::STA_CONNECTING);
    WiFi.reconnect();
  }
}

void ShotStopperNetwork::serviceWifiScan(uint32_t now) {
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  const bool matchingMaintenanceLease =
      scanMaintenanceLeaseId_ != 0 && control.maintenanceLeaseActive &&
      control.maintenanceLeaseId == scanMaintenanceLeaseId_ &&
      !control.activeCycle && !control.relayClosed &&
      !control.physicalPaddleOn;
  const bool safe = controlAllowsConfiguration(control) ||
                    matchingMaintenanceLease;

  bool requested = false;
  WifiScanState state;
  portENTER_CRITICAL(&dataMux_);
  requested = scanRequested_;
  state = scan_.state;
  portEXIT_CRITICAL(&dataMux_);

  if (!safe) {
    const bool canceled = requested || state == WifiScanState::RUNNING ||
                          state == WifiScanState::QUEUED;
    if (state == WifiScanState::RUNNING) {
      esp_wifi_scan_stop();
      WiFi.scanDelete();
    }
    if (canceled) {
      portENTER_CRITICAL(&dataMux_);
      scanRequested_ = false;
      scan_.state = WifiScanState::CANCELED;
      scan_.updatedAtMs = now;
      scan_.count = 0;
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_CANCELED);
      if (scanMaintenanceLeaseId_ != 0) {
        WebCommand command;
        command.type = WebCommandType::START_WIFI_SCAN;
        command.requestId = scanRequestId_;
        command.maintenanceLeaseId = scanMaintenanceLeaseId_;
        (void)enqueueMaintenanceCompletion(
            command, false, CommandResultState::CANCELED);
        scanMaintenanceLeaseId_ = 0;
        scanRequestId_ = 0;
      }
    }
    return;
  }

  if (requested && state == WifiScanState::QUEUED) {
    const int16_t result =
        WiFi.scanNetworks(true, false, false, 120);
    portENTER_CRITICAL(&dataMux_);
    scanRequested_ = false;
    if (result == WIFI_SCAN_RUNNING) {
      scan_.state = WifiScanState::RUNNING;
      scan_.updatedAtMs = now;
    }
    portEXIT_CRITICAL(&dataMux_);
    if (result == WIFI_SCAN_RUNNING) {
      log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_STARTED);
      return;
    }
    finishWifiScan(result, now);
    return;
  }

  if (state != WifiScanState::RUNNING) {
    return;
  }
  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  }
  finishWifiScan(result, now);
}

void ShotStopperNetwork::finishWifiScan(int16_t resultCount, uint32_t now) {
  WifiScanSnapshot completed;
  completed.updatedAtMs = now;
  if (resultCount < 0) {
    completed.state = WifiScanState::FAILED;
    WiFi.scanDelete();
    portENTER_CRITICAL(&dataMux_);
    scan_ = completed;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_ERROR, resultCount);
    if (scanMaintenanceLeaseId_ != 0) {
      WebCommand command;
      command.type = WebCommandType::START_WIFI_SCAN;
      command.requestId = scanRequestId_;
      command.maintenanceLeaseId = scanMaintenanceLeaseId_;
      (void)enqueueMaintenanceCompletion(command, false);
      scanMaintenanceLeaseId_ = 0;
      scanRequestId_ = 0;
    }
    return;
  }

  completed.state = WifiScanState::READY;
  for (int16_t index = 0;
       index < resultCount && completed.count < MAX_WIFI_SCAN_RESULTS;
       ++index) {
    const String ssid = WiFi.SSID(static_cast<uint8_t>(index));
    const size_t length = ssid.length();
    if (length == 0 || length >= WIFI_SSID_CAPACITY) {
      continue;
    }
    const int32_t rssi = WiFi.RSSI(static_cast<uint8_t>(index));
    size_t duplicate = completed.count;
    for (size_t item = 0; item < completed.count; ++item) {
      if (strcmp(completed.networks[item].ssid, ssid.c_str()) == 0) {
        duplicate = item;
        break;
      }
    }
    if (duplicate < completed.count) {
      if (rssi > completed.networks[duplicate].rssi) {
        completed.networks[duplicate].rssi = rssi;
        completed.networks[duplicate].channel =
            static_cast<uint8_t>(WiFi.channel(static_cast<uint8_t>(index)));
        completed.networks[duplicate].open =
            WiFi.encryptionType(static_cast<uint8_t>(index)) == WIFI_AUTH_OPEN;
      }
      continue;
    }
    WifiScanNetwork &network = completed.networks[completed.count++];
    memcpy(network.ssid, ssid.c_str(), length);
    network.ssid[length] = '\0';
    network.rssi = rssi;
    network.channel =
        static_cast<uint8_t>(WiFi.channel(static_cast<uint8_t>(index)));
    network.open =
        WiFi.encryptionType(static_cast<uint8_t>(index)) == WIFI_AUTH_OPEN;
  }

  for (size_t outer = 1; outer < completed.count; ++outer) {
    WifiScanNetwork current = completed.networks[outer];
    size_t inner = outer;
    while (inner > 0 && completed.networks[inner - 1].rssi < current.rssi) {
      completed.networks[inner] = completed.networks[inner - 1];
      --inner;
    }
    completed.networks[inner] = current;
  }
  WiFi.scanDelete();
  portENTER_CRITICAL(&dataMux_);
  scan_ = completed;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::WIFI_SCAN_COMPLETE,
      completed.count, resultCount);
  if (scanMaintenanceLeaseId_ != 0) {
    WebCommand command;
    command.type = WebCommandType::START_WIFI_SCAN;
    command.requestId = scanRequestId_;
    command.maintenanceLeaseId = scanMaintenanceLeaseId_;
    (void)enqueueMaintenanceCompletion(command, true);
    scanMaintenanceLeaseId_ = 0;
    scanRequestId_ = 0;
  }
}

void ShotStopperNetwork::serviceSessions(uint32_t now) {
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  bool anyAuthenticated = false;
  uint32_t newestHeartbeat = 0;
  bool ownerAuthenticated = false;
  uint32_t ownerHeartbeat = 0;
  uint8_t expiredSessions = 0;
  portENTER_CRITICAL(&dataMux_);
  for (WebSession &session : sessions_) {
    if (!session.active) {
      continue;
    }
    if (static_cast<uint32_t>(now - session.lastHeartbeatMs) >= UI_GRACE_MS) {
      session.active = false;
      memset(session.token, 0, sizeof(session.token));
      memset(session.csrf, 0, sizeof(session.csrf));
      ++expiredSessions;
      continue;
    }
    anyAuthenticated = true;
    if (control.activeCycle && control.source == ControlSource::WEB &&
        session.id == control.webSessionId) {
      ownerAuthenticated = true;
      ownerHeartbeat = session.lastHeartbeatMs;
    }
    if (static_cast<int32_t>(session.lastHeartbeatMs - newestHeartbeat) > 0) {
      newestHeartbeat = session.lastHeartbeatMs;
    }
  }
  status_.uiAuthenticated = anyAuthenticated;
  portEXIT_CRITICAL(&dataMux_);
  if (expiredSessions > 0) {
    log(DebugCategory::WEB, DebugCode::UI_EXPIRED, expiredSessions);
  }

  if (control.activeCycle && control.source == ControlSource::WEB &&
      control.relayClosed &&
      (!ownerAuthenticated ||
       static_cast<uint32_t>(now - ownerHeartbeat) >=
           WEB_PADDLE_HEARTBEAT_TIMEOUT_MS)) {
    if (!heartbeatStopSent_) {
      WebCommand stop;
      stop.type = WebCommandType::STOP_HEARTBEAT;
      stop.requestId = allocateRequestId();
      stop.webSessionId = control.webSessionId;
      stop.controlLeaseId = control.controlLeaseId;
      heartbeatStopSent_ = callbacks_.enqueueWebCommand(stop);
    }
  } else {
    heartbeatStopSent_ = false;
  }

  // A successful STA connection is the permanent operating path. Sessions
  // still expire and Web-origin control still requires heartbeats, but neither
  // the HTTP server nor STA are ever shut down by the UI visibility timer.
  const NetworkStatusSnapshot network = snapshot();
  if (!network.apActive) {
    networkShutdownPending_ = false;
    portENTER_CRITICAL(&dataMux_);
    status_.windowRemainingMs = 0;
    portEXIT_CRITICAL(&dataMux_);
    return;
  }

  if (anyAuthenticated) {
    networkShutdownPending_ = false;
    const uint32_t age = static_cast<uint32_t>(now - newestHeartbeat);
    portENTER_CRITICAL(&dataMux_);
    lastAuthenticatedAtMs_ = newestHeartbeat;
    status_.windowRemainingMs = age >= UI_GRACE_MS ? 0 : UI_GRACE_MS - age;
    portEXIT_CRITICAL(&dataMux_);
    return;
  }

  bool everAuthenticated;
  uint32_t lastAuthenticated;
  portENTER_CRITICAL(&dataMux_);
  everAuthenticated = everAuthenticated_;
  lastAuthenticated = lastAuthenticatedAtMs_;
  portEXIT_CRITICAL(&dataMux_);
  const uint32_t reference =
      everAuthenticated ? lastAuthenticated : networkStartedAtMs_;
  const uint32_t timeout = everAuthenticated ? UI_GRACE_MS : AP_WINDOW_MS;
  const uint32_t age = static_cast<uint32_t>(now - reference);
  networkShutdownPending_ = age >= timeout;
  portENTER_CRITICAL(&dataMux_);
  status_.windowRemainingMs = age >= timeout ? 0 : timeout - age;
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::processAcceptedCommands() {
  if (acceptedCommandQueue_ == nullptr) {
    return;
  }

  if (completionPending_) {
    if (!callbacks_.enqueueWebCommand(completionCommand_)) {
      return;
    }
    completionPending_ = false;
    completionCommand_ = WebCommand{};
  }

  const uint32_t now = millis();
  if (!acceptedCommandPending_) {
    if (xQueueReceive(acceptedCommandQueue_, &acceptedCommand_, 0) != pdTRUE) {
      return;
    }
    acceptedCommandPending_ = true;
    acceptedCommandAttempts_ = 0;
    acceptedCommandReceivedAtMs_ = now;
    acceptedCommandRetryAtMs_ = now;
  }
  if (static_cast<int32_t>(now - acceptedCommandRetryAtMs_) < 0) {
    return;
  }

  if (acceptedCommand_.type == WebCommandType::MAINTENANCE_COMPLETE &&
      acceptedCommand_.maintenanceLeaseId == 0) {
    const CommandResultState terminalState =
        acceptedCommand_.resultState == CommandResultState::APPLIED ||
                acceptedCommand_.resultState == CommandResultState::PERSISTED ||
                acceptedCommand_.resultState == CommandResultState::FAILED ||
                acceptedCommand_.resultState == CommandResultState::CANCELED
            ? acceptedCommand_.resultState
            : (acceptedCommand_.succeeded ? CommandResultState::APPLIED
                                          : CommandResultState::CANCELED);
    recordCommandResult(
        acceptedCommand_.requestId, terminalState);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  const bool matchingLease =
      acceptedCommand_.maintenanceLeaseId != 0 &&
      control.maintenanceLeaseActive &&
      control.maintenanceLeaseId == acceptedCommand_.maintenanceLeaseId;
  if (!matchingLease) {
    // The control task publishes the lease after enqueueing the command. Wait
    // for that publication, but reject a lease that was canceled/replaced.
    const bool publicationTimedOut =
        static_cast<uint32_t>(now - acceptedCommandReceivedAtMs_) >=
        MAINTENANCE_PUBLICATION_TIMEOUT_MS;
    if (control.maintenanceLeaseActive || control.activeCycle ||
        control.physicalPaddleOn || publicationTimedOut) {
      (void)enqueueMaintenanceCompletion(
          acceptedCommand_, false, CommandResultState::CANCELED);
      acceptedCommandPending_ = false;
      acceptedCommand_ = WebCommand{};
    }
    return;
  }
  if (control.activeCycle || control.relayClosed ||
      control.physicalPaddleOn) {
    (void)enqueueMaintenanceCompletion(
        acceptedCommand_, false, CommandResultState::CANCELED);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  recordCommandResult(acceptedCommand_.requestId,
                      CommandResultState::RESERVED);
  ++acceptedCommandAttempts_;
  if (!processAcceptedCommand(acceptedCommand_)) {
    if (acceptedCommandAttempts_ < COMMAND_MAX_ATTEMPTS) {
      const uint32_t backoff =
          COMMAND_RETRY_MIN_MS * acceptedCommandAttempts_;
      acceptedCommandRetryAtMs_ = now + backoff;
      log(DebugCategory::SECURITY, DebugCode::COMMAND_RETRY,
          acceptedCommand_.requestId, acceptedCommandAttempts_);
      return;
    }
    (void)enqueueMaintenanceCompletion(acceptedCommand_, false);
    log(DebugCategory::SECURITY, DebugCode::COMMAND_FAILED,
        acceptedCommand_.requestId, acceptedCommandAttempts_);
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
    return;
  }

  if (acceptedCommand_.type != WebCommandType::START_WIFI_SCAN) {
    (void)enqueueMaintenanceCompletion(acceptedCommand_, true);
  }
  acceptedCommandPending_ = false;
  acceptedCommand_ = WebCommand{};
}

bool ShotStopperNetwork::enqueueMaintenanceCompletion(
    const WebCommand &command, bool succeeded,
    CommandResultState failureState) {
  WebCommand result;
  result.type = WebCommandType::MAINTENANCE_COMPLETE;
  result.requestId = command.requestId;
  result.maintenanceLeaseId = command.maintenanceLeaseId;
  result.config = command.config;
  result.succeeded = succeeded;
  recordCommandResult(
      command.requestId,
      succeeded ? (command.type == WebCommandType::PERSIST_RUNTIME ||
                           command.type == WebCommandType::SAVE_NETWORK ||
                           command.type == WebCommandType::FORGET_NETWORK ||
                           command.type == WebCommandType::CHANGE_AP_PASSWORD ||
                           command.type == WebCommandType::RESET_NETWORK_UI ||
                           command.type == WebCommandType::FACTORY_RESET
                       ? CommandResultState::PERSISTED
                       : CommandResultState::APPLIED)
                : failureState);
  if (callbacks_.enqueueWebCommand(result)) {
    return true;
  }
  completionCommand_ = result;
  completionPending_ = true;
  return false;
}

bool ShotStopperNetwork::processAcceptedCommand(const WebCommand &command) {
  PersistedSettings next = settingsCopy();
  bool persist = false;
  bool factoryReset = false;
  bool authenticationChanged = false;
  switch (command.type) {
    case WebCommandType::PERSIST_RUNTIME:
      if (validateRuntimeConfig(command.config) != ConfigValidationError::NONE) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      next.runtime = command.config;
      persist = true;
      break;

    case WebCommandType::SAVE_NETWORK:
      if (!validWifiSsid(command.ssid) ||
          !validWifiPassword(command.password, command.openNetwork)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      next.staConfigured = true;
      next.staOpen = command.openNetwork;
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      strncpy(next.staSsid, command.ssid, sizeof(next.staSsid) - 1);
      strncpy(next.staPassword, command.password,
              sizeof(next.staPassword) - 1);
      persist = true;
      restartPending_ = true;
      break;

    case WebCommandType::FORGET_NETWORK:
      next.staConfigured = false;
      next.staOpen = false;
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      persist = true;
      restartPending_ = true;
      break;

    case WebCommandType::CHANGE_AP_PASSWORD:
      if (!refreshAuthentication(next, command.password)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      persist = true;
      authenticationChanged = true;
      apRestartPending_ = snapshot().apActive;
      break;

    case WebCommandType::RESET_NETWORK_UI:
      next.staConfigured = false;
      next.staOpen = false;
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      if (!initializeDefaultAuthentication(next)) {
        return false;
      }
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::NETWORK_RESET);
      break;

    case WebCommandType::FACTORY_RESET:
      if (!resetPersistedSettingsToFactory(next)) {
        restartPending_ = false;
        apRestartPending_ = false;
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      if (callbacks_.clearShotLog != nullptr) {
        callbacks_.clearShotLog();
      }
      factoryReset = true;
      authenticationChanged = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::FACTORY_RESET);
      break;

    case WebCommandType::RESTART:
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::RESTART_REQUESTED);
      break;

    case WebCommandType::START_WIFI_SCAN:
      portENTER_CRITICAL(&dataMux_);
      scan_ = WifiScanSnapshot{};
      scan_.state = WifiScanState::QUEUED;
      scan_.updatedAtMs = millis();
      scanRequested_ = true;
      scanMaintenanceLeaseId_ = command.maintenanceLeaseId;
      scanRequestId_ = command.requestId;
      portEXIT_CRITICAL(&dataMux_);
      return true;

    default:
      return false;
  }

  if (persist) {
    if (!savePersistedSettings(next)) {
      restartPending_ = false;
      apRestartPending_ = false;
      log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
      return false;
    }
    portENTER_CRITICAL(&dataMux_);
    settings_ = next;
    status_.wifiConfigured = next.staConfigured;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  } else if (factoryReset) {
    portENTER_CRITICAL(&dataMux_);
    settings_ = next;
    status_.wifiConfigured = false;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  }

  if (restartPending_ || apRestartPending_ || authenticationChanged) {
    restartRequestedAtMs_ = millis();
    invalidateAllSessions();
  }
  return true;
}

void ShotStopperNetwork::log(DebugCategory category, DebugCode code,
                             int32_t argument1, int32_t argument2) {
  if (callbacks_.addDebugEvent != nullptr) {
    callbacks_.addDebugEvent(category, code, argument1, argument2);
  }
}

void ShotStopperNetwork::ntpSyncNotificationCallback(struct timeval *tv) {
  if (tv == nullptr) {
    return;
  }
  g_wallClock.queueSyncFromCallback(static_cast<uint32_t>(tv->tv_sec));
}

void ShotStopperNetwork::stopNtp() {
  if (ntpStarted_ || esp_sntp_enabled()) {
    esp_sntp_stop();
    ntpStarted_ = false;
  }
}

bool ShotStopperNetwork::ntpMayArm(uint32_t now, bool staConnected) const {
  if (!staConnected || server_ == nullptr) {
    return false;
  }
  if (staNtpEligibleAtMs_ != 0 &&
      static_cast<int32_t>(now - staNtpEligibleAtMs_) < 0) {
    return false;
  }
  return true;
}

void ShotStopperNetwork::armNtp(uint32_t now) {
  resolveNtpServerHost(settings_.runtime, ntpFailoverIndex_, ntpServerBuffer_);
  g_wallClock.setSyncing(ntpServerBuffer_, now);
  ntpSyncStartedAtMs_ = now;

  if (esp_sntp_enabled()) {
    esp_sntp_setservername(0, ntpServerBuffer_);
    sntp_restart();
    ntpStarted_ = true;
    return;
  }

  esp_sntp_set_time_sync_notification_cb(ntpSyncNotificationCallback);
  configTime(0, 0, ntpServerBuffer_);
  ntpStarted_ = true;
}

void ShotStopperNetwork::handleNtpFailure(uint32_t now) {
  stopNtp();
  if (ntpFailoverIndex_ < 3) {
    ++ntpFailoverIndex_;
  }
  g_wallClock.markFailed(now, NTP_MAX_CONSECUTIVE_FAILURES);
  log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_FAIL, ntpFailoverIndex_, 0);
}

void ShotStopperNetwork::serviceNtp(uint32_t now, bool staConnected) {
  if (g_wallClock.applyPendingSync(now)) {
    ntpFailoverIndex_ = 0;
    log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_OK);
  }

  if (settings_.runtime.revision != ntpConfigRevision_) {
    ntpConfigRevision_ = settings_.runtime.revision;
    ntpRearmPending_ = true;
  }

  if (!staConnected) {
    stopNtp();
    staNtpEligibleAtMs_ = 0;
    g_wallClock.markDisabled();
    return;
  }

  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(now);

  if (timeStatus.state == TimeSyncState::SYNCING) {
    if (applySystemTimeToWallClock(now)) {
      ntpFailoverIndex_ = 0;
      log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_OK);
    } else if (static_cast<uint32_t>(now - ntpSyncStartedAtMs_) >=
               NTP_FIRST_SYNC_TIMEOUT_MS) {
      handleNtpFailure(now);
    }
    return;
  }

  if (!ntpMayArm(now, staConnected)) {
    return;
  }

  if (ntpRearmPending_ || ntpManualSyncPending_ || ntpActivitySyncPending_) {
    ntpRearmPending_ = false;
    ntpManualSyncPending_ = false;
    ntpActivitySyncPending_ = false;
    ntpFailoverIndex_ = 0;
    armNtp(now);
    return;
  }

  switch (timeStatus.state) {
    case TimeSyncState::OFF:
    case TimeSyncState::FAILED:
      if (timeStatus.nextRetryInMs > 0) {
        return;
      }
      armNtp(now);
      return;
    case TimeSyncState::SYNCED:
    case TimeSyncState::STALE:
      if (timeStatus.lastSyncAgeMs >= NTP_RESYNC_INTERVAL_MS &&
          applySystemTimeToWallClock(now)) {
        ntpFailoverIndex_ = 0;
        log(DebugCategory::NETWORK, DebugCode::TIME_SYNC_OK);
      }
      return;
    case TimeSyncState::SYNCING:
      return;
  }
}

bool ShotStopperNetwork::startHttpServer() {
  if (server_ != nullptr) {
    return true;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = tskIDLE_PRIORITY + 1;
  config.stack_size = 12288;
  config.max_open_sockets = 2;
  config.max_uri_handlers = 23;
  config.max_resp_headers = 8;
  config.backlog_conn = 2;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 2;
  config.send_wait_timeout = 2;
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    return false;
  }

  const bool registered =
      registerHandler(server_, "/", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/api/v1/login", HTTP_POST, loginHandler) &&
      registerHandler(server_, "/api/v1/logout", HTTP_POST, logoutHandler) &&
      registerHandler(server_, "/api/v1/heartbeat", HTTP_POST,
                      heartbeatHandler) &&
      registerHandler(server_, "/api/v1/status", HTTP_GET, statusHandler) &&
      registerHandler(server_, "/api/v1/log", HTTP_GET, logHandler) &&
      registerHandler(server_, "/api/v1/shots", HTTP_GET, shotsHandler) &&
      registerHandler(server_, "/api/v1/shots/clear", HTTP_POST,
                      shotsClearHandler) &&
      registerHandler(server_, "/api/v1/shots/delete", HTTP_POST,
                      shotsDeleteHandler) &&
      registerHandler(server_, "/api/v1/time/sync", HTTP_POST,
                      timeSyncHandler) &&
      registerHandler(server_, "/api/v1/config", HTTP_POST, configHandler) &&
      registerHandler(server_, "/api/v1/calibration/reset", HTTP_POST,
                      resetCalibrationHandler) &&
      registerHandler(server_, "/api/v1/control/paddle", HTTP_POST,
                      paddleHandler) &&
      registerHandler(server_, "/api/v1/control/rinse", HTTP_POST,
                      rinseHandler) &&
      registerHandler(server_, "/api/v1/control/stop", HTTP_POST,
                      stopHandler) &&
      registerHandler(server_, "/api/v1/control/restart", HTTP_POST,
                      restartHandler) &&
      registerHandler(server_, "/api/v1/factory-reset", HTTP_POST,
                      factoryResetHandler) &&
      registerHandler(server_, "/api/v1/network", HTTP_POST, networkHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_POST,
                      wifiScanStartHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_GET,
                      wifiScanStatusHandler) &&
      registerHandler(server_, "/api/v1/access-point/password", HTTP_POST,
                      apPasswordHandler);
  if (!registered) {
    stopHttpServer();
  }
  return registered;
}

void ShotStopperNetwork::stopHttpServer() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
}

void ShotStopperNetwork::randomHex(char output[TOKEN_HEX_CAPACITY]) {
  static constexpr char digits[] = "0123456789abcdef";
  uint8_t random[TOKEN_BYTES] = {};
  esp_fill_random(random, sizeof(random));
  for (size_t index = 0; index < sizeof(random); ++index) {
    output[index * 2] = digits[random[index] >> 4U];
    output[index * 2 + 1] = digits[random[index] & 0x0FU];
  }
  output[TOKEN_HEX_CAPACITY - 1] = '\0';
}

bool ShotStopperNetwork::createSession(char token[TOKEN_HEX_CAPACITY],
                                       char csrf[TOKEN_HEX_CAPACITY]) {
  randomHex(token);
  randomHex(csrf);
  const uint32_t now = millis();
  bool created = false;
  bool replaced = false;
  uint32_t replacedSessionId = 0;
  const uint32_t newSessionId = allocateSessionId();
  portENTER_CRITICAL(&dataMux_);
  size_t sessionIndex = SESSION_COUNT;
  uint32_t oldestHeartbeat = 0;
  for (size_t index = 0; index < SESSION_COUNT; ++index) {
    const WebSession &session = sessions_[index];
    if (!session.active) {
      sessionIndex = index;
      break;
    }
    if (sessionIndex == SESSION_COUNT ||
        static_cast<int32_t>(session.lastHeartbeatMs - oldestHeartbeat) < 0) {
      sessionIndex = index;
      oldestHeartbeat = session.lastHeartbeatMs;
    }
  }
  if (sessionIndex < SESSION_COUNT) {
    replaced = sessions_[sessionIndex].active;
    replacedSessionId = sessions_[sessionIndex].id;
    WebSession &session = sessions_[sessionIndex];
    session = WebSession{};
    session.active = true;
    session.id = newSessionId;
    session.lastHeartbeatMs = now;
    memcpy(session.token, token, sizeof(session.token));
    memcpy(session.csrf, csrf, sizeof(session.csrf));
    created = true;
    everAuthenticated_ = true;
    lastAuthenticatedAtMs_ = now;
    status_.uiAuthenticated = true;
  }
  portEXIT_CRITICAL(&dataMux_);
  if (replaced) {
    // A bounded session set must not prevent a legitimate new login. The
    // least recently active client is invalidated before issuing the new
    // credentials, so it cannot retain control privileges.
    log(DebugCategory::SECURITY, DebugCode::UI_REPLACED);
    requestStopForSession(replacedSessionId);
  }
  return created;
}

bool ShotStopperNetwork::authenticate(httpd_req_t *request, bool requireCsrf,
                                      size_t *sessionIndex) {
  char token[TOKEN_HEX_CAPACITY] = {};
  char csrf[TOKEN_HEX_CAPACITY] = {};
  if (httpd_req_get_hdr_value_len(request, "X-Session-Token") !=
          TOKEN_HEX_CAPACITY - 1 ||
      httpd_req_get_hdr_value_str(request, "X-Session-Token", token,
                                  sizeof(token)) != ESP_OK) {
    return false;
  }
  if (requireCsrf &&
      (httpd_req_get_hdr_value_len(request, "X-CSRF-Token") !=
           TOKEN_HEX_CAPACITY - 1 ||
       httpd_req_get_hdr_value_str(request, "X-CSRF-Token", csrf,
                                   sizeof(csrf)) != ESP_OK)) {
    return false;
  }

  bool accepted = false;
  portENTER_CRITICAL(&dataMux_);
  for (size_t index = 0; index < SESSION_COUNT; ++index) {
    const WebSession &session = sessions_[index];
    if (session.active &&
        constantTimeTokenEqual(session.token, token, sizeof(token)) &&
        (!requireCsrf ||
         constantTimeTokenEqual(session.csrf, csrf, sizeof(csrf)))) {
      if (sessionIndex != nullptr) {
        *sessionIndex = index;
      }
      accepted = true;
      break;
    }
  }
  portEXIT_CRITICAL(&dataMux_);
  return accepted;
}

void ShotStopperNetwork::invalidateSession(size_t index, DebugCode code) {
  if (index >= SESSION_COUNT) {
    return;
  }
  uint32_t invalidatedSessionId = 0;
  portENTER_CRITICAL(&dataMux_);
  invalidatedSessionId = sessions_[index].id;
  sessions_[index] = WebSession{};
  portEXIT_CRITICAL(&dataMux_);
  requestStopForSession(invalidatedSessionId);
  log(DebugCategory::WEB, code);
}

void ShotStopperNetwork::invalidateAllSessions() {
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  portENTER_CRITICAL(&dataMux_);
  for (WebSession &session : sessions_) {
    session = WebSession{};
  }
  status_.uiAuthenticated = false;
  portEXIT_CRITICAL(&dataMux_);
  if (control.activeCycle && control.source == ControlSource::WEB) {
    requestStopForSession(control.webSessionId);
  }
}

void ShotStopperNetwork::requestStopForSession(uint32_t webSessionId) {
  if (webSessionId == 0) {
    return;
  }
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  if (!control.activeCycle || control.source != ControlSource::WEB ||
      control.webSessionId != webSessionId) {
    return;
  }
  WebCommand stop;
  stop.type = WebCommandType::STOP_HEARTBEAT;
  stop.requestId = allocateRequestId();
  stop.webSessionId = webSessionId;
  stop.controlLeaseId = control.controlLeaseId;
  heartbeatStopSent_ = callbacks_.enqueueWebCommand(stop) ||
                       heartbeatStopSent_;
}

bool ShotStopperNetwork::loginRateLimited(uint32_t now) {
  portENTER_CRITICAL(&dataMux_);
  if (static_cast<uint32_t>(now - loginWindowStartedAtMs_) >= 60000) {
    loginWindowStartedAtMs_ = now;
    loginAttemptsInWindow_ = 0;
  }
  const bool limited = loginAttemptsInWindow_ >= 5;
  if (!limited) {
    ++loginAttemptsInWindow_;
  }
  portEXIT_CRITICAL(&dataMux_);
  return limited;
}

esp_err_t ShotStopperNetwork::sendJson(httpd_req_t *request,
                                       const char *status,
                                       const char *json) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(request, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ShotStopperNetwork::sendError(httpd_req_t *request,
                                        const char *status,
                                        const char *error,
                                        const char *message) {
  char body[256] = {};
  snprintf(body, sizeof(body), "{\"error\":\"%s\",\"message\":\"%s\"}",
           error, message);
  return sendJson(request, status, body);
}

bool ShotStopperNetwork::requireJsonContentType(httpd_req_t *request) {
  const size_t length =
      httpd_req_get_hdr_value_len(request, "Content-Type");
  if (length == 0 || length >= 64) {
    return false;
  }
  char contentType[64] = {};
  if (httpd_req_get_hdr_value_str(request, "Content-Type", contentType,
                                   sizeof(contentType)) != ESP_OK) {
    return false;
  }
  const size_t jsonLength = strlen(JSON_CONTENT_TYPE);
  if (strncmp(contentType, JSON_CONTENT_TYPE, jsonLength) != 0) {
    return false;
  }
  const char suffix = contentType[jsonLength];
  return suffix == '\0' || suffix == ';' || suffix == ' ' || suffix == '\t';
}

bool ShotStopperNetwork::readJsonBody(
    httpd_req_t *request, char body[REQUEST_BODY_CAPACITY]) {
  if (!requireJsonContentType(request) || request->content_len <= 0 ||
      request->content_len >= REQUEST_BODY_CAPACITY) {
    return false;
  }
  size_t received = 0;
  while (received < static_cast<size_t>(request->content_len)) {
    const int result = httpd_req_recv(
        request, body + received,
        static_cast<size_t>(request->content_len) - received);
    if (result <= 0) {
      return false;
    }
    received += static_cast<size_t>(result);
  }
  body[received] = '\0';
  return true;
}

esp_err_t ShotStopperNetwork::rootHandler(httpd_req_t *request) {
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(
      request, "Content-Security-Policy",
      "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
  return httpd_resp_send(request, SHOT_STOPPER_WEB_UI,
                         HTTPD_RESP_USE_STRLEN);
}

esp_err_t ShotStopperNetwork::loginHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const uint32_t now = millis();
  if (self.loginRateLimited(now)) {
    return sendError(request, STATUS_TOO_MANY, "LOGIN_RATE_LIMITED",
                     "Too many attempts; wait one minute.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A bounded JSON body is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char password[WIFI_PASSWORD_CAPACITY] = {};
  static const char *const fields[] = {"password"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonString(root, "password", password,
                                 sizeof(password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed || !verifyAdminPassword(self.settingsCopy(), password)) {
    memset(password, 0, sizeof(password));
    self.log(DebugCategory::WEB, DebugCode::WEB_COMMAND_REJECTED);
    return sendError(request, STATUS_UNAUTHORIZED, "INVALID_CREDENTIALS",
                     "Incorrect password.");
  }
  memset(password, 0, sizeof(password));

  char token[TOKEN_HEX_CAPACITY] = {};
  char csrf[TOKEN_HEX_CAPACITY] = {};
  if (!self.createSession(token, csrf)) {
    return sendError(request, STATUS_UNAVAILABLE, "SESSION_LIMIT",
                     "Could not create the session.");
  }
  char response[160] = {};
  snprintf(response, sizeof(response),
           "{\"token\":\"%s\",\"csrf\":\"%s\"}", token, csrf);
  self.log(DebugCategory::WEB, DebugCode::UI_LOGIN);
  return sendJson(request, STATUS_OK, response);
}

esp_err_t ShotStopperNetwork::logoutHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  size_t sessionIndex = 0;
  if (!self.authenticate(request, true, &sessionIndex)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  self.invalidateSession(sessionIndex, DebugCode::UI_LOGOUT);
  return sendJson(request, STATUS_OK, "{\"ok\":true}");
}

esp_err_t ShotStopperNetwork::heartbeatHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  size_t sessionIndex = 0;
  if (!self.authenticate(request, true, &sessionIndex)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  const uint32_t now = millis();
  portENTER_CRITICAL(&self.dataMux_);
  self.sessions_[sessionIndex].lastHeartbeatMs = now;
  self.lastAuthenticatedAtMs_ = now;
  self.status_.uiAuthenticated = true;
  portEXIT_CRITICAL(&self.dataMux_);
  return sendJson(request, STATUS_OK, "{\"ok\":true}");
}

const char *ShotStopperNetwork::stateLabel(StopperState state) {
  switch (state) {
    case StopperState::REQUIRES_OFF: return "Paddle release required";
    case StopperState::READY: return "Ready";
    case StopperState::QUALIFYING_ON: return "Qualifying gesture";
    case StopperState::BREW: return "Automatic brew";
    case StopperState::RINSE: return "Rinse in progress";
    case StopperState::MANUAL_NO_SCALE:
      return "Manual shot without scale";
  }
  return "Unknown";
}

const char *ShotStopperNetwork::controlSourceName(ControlSource source) {
  switch (source) {
    case ControlSource::NONE: return "none";
    case ControlSource::PHYSICAL: return "physical";
    case ControlSource::WEB: return "web";
  }
  return "unknown";
}

const char *ShotStopperNetwork::endReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "NONE";
    case EndReason::PADDLE: return "PADDLE";
    case EndReason::SCALE_PREDICTION: return "SCALE_PREDICTION";
    case EndReason::SCALE_THRESHOLD: return "SCALE_THRESHOLD";
    case EndReason::WEIGHT_ANOMALY: return "WEIGHT_ANOMALY";
    case EndReason::GLOBAL_LIMIT: return "GLOBAL_LIMIT";
    case EndReason::CONFIGURED_WALL_LIMIT:
      return "CONFIGURED_WALL_LIMIT";
    case EndReason::SHORT_SHOT: return "SHORT_SHOT";
    case EndReason::RINSE_COMPLETE: return "RINSE_COMPLETE";
    case EndReason::WEB_STOP: return "WEB_STOP";
    case EndReason::PHYSICAL_OVERRIDE: return "PHYSICAL_OVERRIDE";
    case EndReason::WEB_HEARTBEAT_TIMEOUT:
      return "WEB_HEARTBEAT_TIMEOUT";
    case EndReason::RELAY_SAFETY_FAILURE: return "RELAY_SAFETY_FAILURE";
  }
  return "UNKNOWN";
}

const char *ShotStopperNetwork::staStateName(StaState state) {
  switch (state) {
    case StaState::NOT_CONFIGURED: return "NOT_CONFIGURED";
    case StaState::CONNECTING: return "CONNECTING";
    case StaState::CONNECTED: return "CONNECTED";
    case StaState::FAILED: return "FAILED";
    case StaState::DISCONNECTED: return "DISCONNECTED";
  }
  return "UNKNOWN";
}

const char *ShotStopperNetwork::wifiScanStateName(WifiScanState state) {
  switch (state) {
    case WifiScanState::IDLE: return "IDLE";
    case WifiScanState::QUEUED: return "QUEUED";
    case WifiScanState::RUNNING: return "RUNNING";
    case WifiScanState::READY: return "READY";
    case WifiScanState::FAILED: return "FAILED";
    case WifiScanState::CANCELED: return "CANCELED";
  }
  return "UNKNOWN";
}

esp_err_t ShotStopperNetwork::statusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // Status intentionally has no authentication requirement. It is the
  // read-only landing view and contains no credentials, session material, or
  // actionable state. Every mutating route remains authenticated.
  ControlStatusSnapshot control;
  self.callbacks_.copyControlStatus(control);
  const NetworkStatusSnapshot network = self.snapshot();
  char currentWeight[32] = "null";
  char observedWeight[32] = "null";
  char lastWeight[32] = "null";
  if (control.currentWeightValid) {
    snprintf(currentWeight, sizeof(currentWeight), "%.2f",
             static_cast<double>(control.currentWeightG));
  }
  if (control.observedWeightValid) {
    snprintf(observedWeight, sizeof(observedWeight), "%.2f",
             static_cast<double>(control.observedWeightG));
  }
  if (control.lastCycle.weightValid) {
    snprintf(lastWeight, sizeof(lastWeight), "%.2f",
             static_cast<double>(control.lastCycle.lastWeightG));
  }

  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
  char safeNtpCustom[NTP_SERVER_HOST_CAPACITY] = {};
  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  sanitizeJsonEmbed(control.config.ntpServerCustom, safeNtpCustom,
                    sizeof(safeNtpCustom));
  sanitizeJsonEmbed(timeStatus.activeServer, safeActiveServer,
                    sizeof(safeActiveServer));
  const int written = snprintf(
      g_statusResponseBuffer, sizeof(g_statusResponseBuffer),
      "{\"state\":\"%s\",\"stateLabel\":\"%s\",\"relayClosed\":%s,"
      "\"physicalPaddleOn\":%s,\"virtualPaddleOn\":%s,"
      "\"remoteControlEnabled\":%s,"
      "\"controlSource\":\"%s\",\"cn9ElapsedMs\":%lu,"
      "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
      "\"generation\":%lu,\"timersReady\":%s,"
      "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
      "\"feedbackClosed\":%s,\"resetReasonCode\":%lu,"
      "\"unsafeResetCount\":%lu,\"recoveryRequired\":%s,"
      "\"bootLoopDetected\":%s},"
      "\"maintenance\":{\"active\":%s,\"leaseId\":%lu,"
      "\"startedAtMs\":%lu},"
      "\"configMutable\":%s,\"config\":{\"revision\":%lu,"
      "\"goalWeightG\":%u,\"autoTare\":%s,\"timerOnly\":%s,"
      "\"canTareStartTimer\":%s,\"brewConfirmationBeep\":%s,"
      "\"paddleReturnReminderBeep\":%s,"
      "\"paddleReturnReminderIntervalMs\":%lu,"
      "\"paddleReturnReminderMaxDurationMs\":%lu,\"rinseGestureMs\":%lu,"
      "\"rinseDurationMs\":%lu,"
      "\"autoRetare\":%s,\"retareWindowMs\":%lu,"
      "\"minimumCupWeightG\":%.1f,"
      "\"retareStabilitySamples\":%u,"
      "\"retareStabilityToleranceG\":%.1f,"
      "\"retareStabilityMaxGapMs\":%lu,"
      "\"retareStabilityMinDurationMs\":%lu,"
      "\"confirmationTimeoutMs\":%lu,"
      "\"operationalWallMs\":%lu,"
      "\"timezoneOffsetMinutes\":%d,"
      "\"ntpServerPreset\":\"%s\",\"ntpServerCustom\":\"%s\"},"
      "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
      "\"nextRetryInMs\":%lu,\"consecutiveFailures\":%u,"
      "\"activeServer\":\"%s\"},"
      "\"scale\":{\"available\":%s,\"streamState\":\"%s\","
      "\"controlState\":\"%s\",\"controlAccepted\":%s,"
      "\"currentWeightG\":%s,"
      "\"weightAgeMs\":%lu,\"observedWeightG\":%s,"
      "\"observedWeightAgeMs\":%lu,\"connectionGeneration\":%lu,"
      "\"packetSequence\":%lu,\"packetGaps\":%lu,"
      "\"rejectedPackets\":%lu,\"reconnects\":%lu,"
      "\"lastDisconnectReason\":%u,"
      "\"lastDisconnectReasonName\":\"%s\",\"eventsDropped\":%lu},"
      "\"lastCycle\":{\"valid\":%s,"
      "\"durationMs\":%lu,\"endReason\":\"%s\","
      "\"lastWeightG\":%s,\"weightAgeMs\":%lu},"
      "\"network\":{\"networkActive\":%s,\"uiActive\":%s,"
      "\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
      "\"wifiConfigured\":%s,\"staState\":\"%s\","
      "\"staIp\":\"%s\",\"windowRemainingMs\":%lu,"
      "\"taskAgeMs\":%lu,\"taskStackMinWords\":%lu,"
      "\"startupFailures\":%lu},"
      "\"health\":{\"loopMaxGapMs\":%lu,"
      "\"loopStackMinWords\":%lu,\"scaleStackMinWords\":%lu,"
      "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
      "\"largestFreeHeapBlockBytes\":%lu},"
      "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"},"
      "\"cycle\":{\"active\":%s,\"id\":%lu,\"flowDuringRetare\":%s,"
      "\"firstDropMs\":%lu,\"retareFlowFirstDetectedAtMs\":%lu,"
      "\"firstDropElapsedMs\":%lu},"
      "\"debugEventsDropped\":%lu}",
      stopperStateName(control.state), stateLabel(control.state),
      control.relayClosed ? "true" : "false",
      control.physicalPaddleOn ? "true" : "false",
      control.virtualPaddleOn ? "true" : "false",
      control.remoteControlEnabled ? "true" : "false",
      controlSourceName(control.source),
      static_cast<unsigned long>(control.cn9ElapsedMs),
      relaySafetyStateName(control.safetyState),
      relaySafetyFaultName(control.safetyFault),
      static_cast<unsigned long>(control.safetyGeneration),
      control.safetyTimersReady ? "true" : "false",
      control.taskWatchdogReady ? "true" : "false",
      control.externalSafetyPresent ? "true" : "false",
      control.cn9FeedbackClosed ? "true" : "false",
      static_cast<unsigned long>(control.resetReasonCode),
      static_cast<unsigned long>(control.unsafeResetCount),
      control.resetRecoveryRequired ? "true" : "false",
      control.bootLoopDetected ? "true" : "false",
      control.maintenanceLeaseActive ? "true" : "false",
      static_cast<unsigned long>(control.maintenanceLeaseId),
      static_cast<unsigned long>(control.maintenanceStartedAtMs),
      controlAllowsConfiguration(control) ? "true" : "false",
      static_cast<unsigned long>(control.config.revision),
      static_cast<unsigned>(control.config.goalWeightG),
      control.config.autoTare ? "true" : "false",
      control.config.timerOnly ? "true" : "false",
      control.config.canTareStartTimer ? "true" : "false",
      control.config.brewConfirmationBeep ? "true" : "false",
      control.config.paddleReturnReminderBeep ? "true" : "false",
      static_cast<unsigned long>(
          control.config.paddleReturnReminderIntervalMs),
      static_cast<unsigned long>(
          control.config.paddleReturnReminderMaxDurationMs),
      static_cast<unsigned long>(control.config.rinseGestureMs),
      static_cast<unsigned long>(control.config.rinseDurationMs),
      control.config.autoRetare ? "true" : "false",
      static_cast<unsigned long>(control.config.retareWindowMs),
      static_cast<double>(control.config.minimumCupWeightG),
      static_cast<unsigned>(control.config.retareStabilitySamples),
      static_cast<double>(control.config.retareStabilityToleranceG),
      static_cast<unsigned long>(control.config.retareStabilityMaxGapMs),
      static_cast<unsigned long>(control.config.retareStabilityMinDurationMs),
      static_cast<unsigned long>(control.config.confirmationTimeoutMs),
      static_cast<unsigned long>(control.config.operationalWallMs),
      static_cast<int>(control.config.timezoneOffsetMinutes),
      ntpPresetId(control.config.ntpServerPreset),
      safeNtpCustom,
      timeSyncStateName(timeStatus.state),
      static_cast<unsigned long>(timeStatus.utcSec),
      static_cast<unsigned long>(timeStatus.lastSyncAgeMs),
      static_cast<unsigned long>(timeStatus.nextRetryInMs),
      static_cast<unsigned>(timeStatus.consecutiveFailures),
      safeActiveServer,
      control.scaleAvailable ? "true" : "false",
      weightStreamStateName(control.weightStreamState),
      weightControlStateName(control.weightControlState),
      control.currentWeightValid ? "true" : "false", currentWeight,
      static_cast<unsigned long>(control.currentWeightAgeMs),
      observedWeight,
      static_cast<unsigned long>(control.observedWeightAgeMs),
      static_cast<unsigned long>(control.scaleConnectionGeneration),
      static_cast<unsigned long>(control.scalePacketSequence),
      static_cast<unsigned long>(control.scalePacketGaps),
      static_cast<unsigned long>(control.scaleRejectedPackets),
      static_cast<unsigned long>(control.scaleReconnects),
      static_cast<unsigned>(control.scaleLastDisconnectReason),
      scaleDisconnectReasonName(control.scaleLastDisconnectReason),
      static_cast<unsigned long>(control.scaleEventsDropped),
      control.lastCycle.valid ? "true" : "false",
      static_cast<unsigned long>(control.lastCycle.durationMs),
      endReasonName(control.lastCycle.endReason), lastWeight,
      static_cast<unsigned long>(control.lastCycle.weightAgeAtEndMs),
      network.networkActive ? "true" : "false",
      network.uiAuthenticated ? "true" : "false",
      network.apActive ? "true" : "false", network.apIp,
      static_cast<unsigned>(network.apClients),
      network.wifiConfigured ? "true" : "false",
      staStateName(network.staState), network.staIp,
      static_cast<unsigned long>(network.windowRemainingMs),
      static_cast<unsigned long>(network.taskAgeMs),
      static_cast<unsigned long>(network.taskStackMinWords),
      static_cast<unsigned long>(network.startupFailures),
      static_cast<unsigned long>(control.loopMaxGapMs),
      static_cast<unsigned long>(control.loopStackMinWords),
      static_cast<unsigned long>(control.scaleStackMinWords),
      static_cast<unsigned long>(control.freeHeapBytes),
      static_cast<unsigned long>(control.minimumFreeHeapBytes),
      static_cast<unsigned long>(control.largestFreeHeapBlockBytes),
      static_cast<unsigned long>(network.lastCommandRequestId),
      commandResultStateName(network.lastCommandState),
      control.activeCycle ? "true" : "false",
      static_cast<unsigned long>(control.cycleId),
      control.cycleFlowDuringRetare ? "true" : "false",
      static_cast<unsigned long>(control.cycleFirstDropMs),
      static_cast<unsigned long>(control.cycleRetareFlowFirstDetectedAtMs),
      static_cast<unsigned long>(
          control.cycleFirstDropMs != 0 &&
                  control.cycleStartedAtMs != 0 &&
                  static_cast<int32_t>(control.cycleFirstDropMs -
                                       control.cycleStartedAtMs) >= 0
              ? control.cycleFirstDropMs - control.cycleStartedAtMs
              : 0U),
      static_cast<unsigned long>(control.debugEventsDropped));
  if (written < 0 ||
      static_cast<size_t>(written) >= sizeof(g_statusResponseBuffer)) {
    return sendError(request, "500 Internal Server Error", "STATUS_TOO_LARGE",
                     "Status snapshot exceeds its size limit.");
  }
  return sendJson(request, STATUS_OK, g_statusResponseBuffer);
}

esp_err_t ShotStopperNetwork::logHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // The bounded diagnostic log is intentionally public like Status. It emits
  // only fixed enum-derived messages and numeric arguments; credentials,
  // session material and request payloads are never included.
  uint32_t after = 0;
  const size_t queryLength = httpd_req_get_url_query_len(request);
  if (queryLength > 0 && queryLength < 64) {
    char query[64] = {};
    char value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "after", value, sizeof(value)) == ESP_OK) {
      char *end = nullptr;
      const unsigned long parsed = strtoul(value, &end, 10);
      if (end != value && *end == '\0') {
        after = static_cast<uint32_t>(parsed);
      }
    }
  }

  DebugEvent events[LOG_BATCH_SIZE] = {};
  const size_t count =
      self.callbacks_.copyDebugEvents(after, events, LOG_BATCH_SIZE);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_send_chunk(request, "{\"events\":[", HTTPD_RESP_USE_STRLEN);
  for (size_t index = 0; index < count; ++index) {
    char message[128] = {};
    if (events[index].code == DebugCode::STATE_TRANSITION &&
        events[index].argument1 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        events[index].argument1 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
        events[index].argument2 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        events[index].argument2 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
      snprintf(message, sizeof(message), "%s -> %s",
               stopperStateName(
                   static_cast<StopperState>(events[index].argument1)),
               stopperStateName(
                   static_cast<StopperState>(events[index].argument2)));
    } else if ((events[index].code == DebugCode::WEB_COMMAND_ACCEPTED ||
                events[index].code == DebugCode::WEB_COMMAND_REJECTED) &&
               events[index].argument1 >=
                   static_cast<int32_t>(WebCommandType::PADDLE_ON) &&
               events[index].argument1 <=
                   static_cast<int32_t>(
                       WebCommandType::MAINTENANCE_COMPLETE)) {
      snprintf(message, sizeof(message), "%s: %s",
               debugCodeName(events[index].code),
               webCommandTypeName(
                   static_cast<WebCommandType>(events[index].argument1)));
    } else if (formatScaleSampleDebugMessage(events[index], message,
                                             sizeof(message))) {
    } else {
      strncpy(message, debugCodeName(events[index].code),
              sizeof(message) - 1);
    }
    char item[256] = {};
    snprintf(item, sizeof(item),
             "%s{\"sequence\":%lu,\"atMs\":%lu,\"category\":\"%s\","
             "\"message\":\"%s\",\"argument1\":%ld,\"argument2\":%ld}",
             index == 0 ? "" : ",",
             static_cast<unsigned long>(events[index].sequence),
             static_cast<unsigned long>(events[index].atMs),
             debugCategoryName(events[index].category),
             message,
             static_cast<long>(events[index].argument1),
             static_cast<long>(events[index].argument2));
    if (httpd_resp_send_chunk(request, item, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
      return ESP_FAIL;
    }
  }
  return httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
             ? httpd_resp_send_chunk(request, nullptr, 0)
             : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot control;
  self.callbacks_.copyControlStatus(control);
  static ShotLogRecord records[SHOT_LOG_CAPACITY];
  const size_t count =
      self.callbacks_.copyShotRecords != nullptr
          ? self.callbacks_.copyShotRecords(records, SHOT_LOG_CAPACITY)
          : 0;

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  char header[96] = {};
  snprintf(header, sizeof(header),
           "{\"bootId\":%lu,\"shots\":[",
           static_cast<unsigned long>(control.bootId));
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    return ESP_FAIL;
  }

  for (size_t index = 0; index < count; ++index) {
    const ShotLogRecord &record = records[index];
    char actual[16] = "null";
    char errorG[16] = "null";
    char errorPct[16] = "null";
    char flow[16] = "null";
    char firstDrop[16] = "null";
    if (record.actualWeightCg != SHOT_LOG_WEIGHT_MISSING) {
      snprintf(actual, sizeof(actual), "%.2f",
               static_cast<double>(record.actualWeightCg) / 100.0);
      snprintf(errorG, sizeof(errorG), "%.2f",
               static_cast<double>(record.errorCg) / 100.0);
      if (record.goalWeightG > 0) {
        snprintf(errorPct, sizeof(errorPct), "%.1f",
                 static_cast<double>(record.errorCg) * 100.0 /
                     (static_cast<double>(record.goalWeightG) * 100.0));
      }
    }
    if (record.avgFlowCgS != SHOT_LOG_METRIC_MISSING) {
      snprintf(flow, sizeof(flow), "%.2f",
               static_cast<double>(record.avgFlowCgS) / 100.0);
    }
    if (record.firstDropDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(firstDrop, sizeof(firstDrop), "%.1f",
               static_cast<double>(record.firstDropDs) / 10.0);
    }
    char item[560] = {};
    snprintf(item, sizeof(item),
             "%s{\"id\":%lu,\"bootId\":%lu,\"endedAtMs\":%lu,"
             "\"hasWallTime\":%s,\"endedAtLocalSec\":%lu,"
             "\"endedAtUnixSec\":%lu,\"timezoneOffsetMinutesAtCommit\":%d,"
             "\"durationS\":%.1f,"
             "\"goalG\":%u,\"actualG\":%s,\"errorG\":%s,\"errorPct\":%s,"
             "\"offsetG\":%.2f,\"avgFlowGS\":%s,\"firstDropS\":%s,"
             "\"shotType\":\"%s\",\"cutType\":\"%s\"}",
             index == 0 ? "" : ",",
             static_cast<unsigned long>(record.id),
             static_cast<unsigned long>(record.bootId),
             static_cast<unsigned long>(record.endedAtMs),
             record.hasWallTime ? "true" : "false",
             static_cast<unsigned long>(record.endedAtLocalSec),
             static_cast<unsigned long>(record.endedAtUnixSec),
             static_cast<int>(record.timezoneOffsetMinutesAtCommit),
             static_cast<double>(record.durationDs) / 10.0,
             static_cast<unsigned>(record.goalWeightG), actual, errorG,
             errorPct,
             static_cast<double>(record.offsetUsedCg) / 100.0, flow,
             firstDrop,
             shotLogTypeName(static_cast<ShotLogType>(record.shotType)),
             shotLogCutName(static_cast<ShotLogCut>(record.cutType)));
    if (httpd_resp_send_chunk(request, item, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
      return ESP_FAIL;
    }
  }
  return httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK
             ? httpd_resp_send_chunk(request, nullptr, 0)
             : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before clearing shot history.");
  }

  char body[REQUEST_BODY_CAPACITY] = {};
  char confirmation[32] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "An explicit confirmation is required.");
  }
  cJSON *root = cJSON_Parse(body);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_SHOT_LOG") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "SHOT_LOG_CLEAR_NOT_CONFIRMED",
                     "The shot-history clear was not explicitly confirmed.");
  }

  if (self.callbacks_.clearShotLog == nullptr ||
      !self.callbacks_.clearShotLog()) {
    return sendError(request, STATUS_UNAVAILABLE, "SHOT_LOG_CLEAR_FAILED",
                     "Shot history could not be cleared.");
  }
  return sendJson(request, STATUS_OK, "{\"cleared\":true}");
}

esp_err_t ShotStopperNetwork::shotsDeleteHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before deleting shot records.");
  }

  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A bounded JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  uint32_t shotId = 0;
  static const char *const fields[] = {"id"};
  const bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonUint32(root, "id", shotId) && shotId != 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A non-zero shot id is required.");
  }

  if (self.callbacks_.deleteShotRecord == nullptr ||
      !self.callbacks_.deleteShotRecord(shotId)) {
    return sendError(request, STATUS_NOT_FOUND, "SHOT_NOT_FOUND",
                     "The requested shot record was not found.");
  }
  return sendJson(request, STATUS_OK, "{\"deleted\":true}");
}

esp_err_t ShotStopperNetwork::timeSyncHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before syncing the clock.");
  }
  self.ntpManualSyncPending_ = true;
  return self.sendAccepted(request, self.allocateRequestId());
}

esp_err_t ShotStopperNetwork::configHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Configuration is locked while a cycle is active.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A bounded JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  RuntimeConfig candidate = status.config;
  char customNtp[NTP_SERVER_HOST_CAPACITY] = {};
  memcpy(customNtp, candidate.ntpServerCustom, sizeof(customNtp));
  static const char *const fields[] = {
      "goalWeightG", "rinseGestureMs", "rinseDurationMs", "operationalWallMs",
      "autoTare", "timerOnly",
      "canTareStartTimer", "brewConfirmationBeep", "paddleReturnReminderBeep",
      "paddleReturnReminderIntervalMs", "paddleReturnReminderMaxDurationMs",
      "autoRetare", "retareWindowMs", "minimumCupWeightG",
      "retareStabilitySamples", "retareStabilityToleranceG",
      "retareStabilityMaxGapMs", "retareStabilityMinDurationMs",
      "confirmationTimeoutMs",
      "timezoneOffsetMinutes", "ntpServerPreset", "ntpServerCustom"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 22) &&
      jsonUint8(root, "goalWeightG", candidate.goalWeightG) &&
      jsonUint32(root, "rinseGestureMs", candidate.rinseGestureMs) &&
      jsonUint32(root, "rinseDurationMs", candidate.rinseDurationMs) &&
      jsonUint32(root, "operationalWallMs", candidate.operationalWallMs) &&
      jsonBoolean(root, "autoTare", candidate.autoTare) &&
      jsonBoolean(root, "timerOnly", candidate.timerOnly) &&
      jsonBoolean(root, "canTareStartTimer", candidate.canTareStartTimer) &&
      jsonBoolean(root, "brewConfirmationBeep",
                  candidate.brewConfirmationBeep) &&
      jsonBoolean(root, "paddleReturnReminderBeep",
                  candidate.paddleReturnReminderBeep) &&
      jsonUint32(root, "paddleReturnReminderIntervalMs",
                 candidate.paddleReturnReminderIntervalMs) &&
      jsonUint32(root, "paddleReturnReminderMaxDurationMs",
                 candidate.paddleReturnReminderMaxDurationMs) &&
      jsonBoolean(root, "autoRetare", candidate.autoRetare) &&
      jsonUint32(root, "retareWindowMs", candidate.retareWindowMs) &&
      jsonFloat(root, "minimumCupWeightG", candidate.minimumCupWeightG) &&
      jsonUint8(root, "retareStabilitySamples",
                candidate.retareStabilitySamples) &&
      jsonFloat(root, "retareStabilityToleranceG",
                candidate.retareStabilityToleranceG) &&
      jsonUint32(root, "retareStabilityMaxGapMs",
                 candidate.retareStabilityMaxGapMs) &&
      jsonUint32(root, "retareStabilityMinDurationMs",
                 candidate.retareStabilityMinDurationMs) &&
      jsonUint32(root, "confirmationTimeoutMs",
                 candidate.confirmationTimeoutMs) &&
      jsonInt16(root, "timezoneOffsetMinutes",
                candidate.timezoneOffsetMinutes) &&
      jsonNtpPreset(root, "ntpServerPreset", candidate.ntpServerPreset) &&
      jsonString(root, "ntpServerCustom", customNtp, sizeof(customNtp), true);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (!parsed) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A field is missing or has an invalid type.");
  }
  memcpy(candidate.ntpServerCustom, customNtp, sizeof(candidate.ntpServerCustom));
  memset(customNtp, 0, sizeof(customNtp));
  const ConfigValidationError error = validateRuntimeConfig(candidate);
  if (error != ConfigValidationError::NONE) {
    self.log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
             static_cast<int32_t>(error));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_CONFIG",
                     configValidationMessage(error));
  }
  WebCommand command;
  command.type = WebCommandType::APPLY_CONFIG;
  command.requestId = self.allocateRequestId();
  command.config = candidate;
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetCalibrationHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Calibration is locked while a cycle is active.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "An empty JSON object is required.");
  }
  cJSON *root = cJSON_Parse(body);
  static const char *const noFields[] = {nullptr};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, noFields, 0);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_REQUEST",
                     "The calibration reset request must be an empty object.");
  }
  WebCommand command;
  command.type = WebCommandType::RESET_WEIGHT_OFFSET;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; calibration was not reset.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::paddleHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  size_t sessionIndex = 0;
  if (!self.authenticate(request, true, &sessionIndex)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON body is required.");
  }
  cJSON *root = cJSON_Parse(body);
  bool on = false;
  static const char *const fields[] = {"on"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonBoolean(root, "on", on);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "The on field must be boolean.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (on && !REMOTE_CN9_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote CN9 actuation is disabled in this firmware build.");
  }
  uint32_t webSessionId = 0;
  portENTER_CRITICAL(&self.dataMux_);
  if (sessionIndex < SESSION_COUNT && self.sessions_[sessionIndex].active) {
    webSessionId = self.sessions_[sessionIndex].id;
  }
  portEXIT_CRITICAL(&self.dataMux_);
  const bool allowed = on ? controlAllowsConfiguration(status)
                          : (status.activeCycle &&
                             status.source == ControlSource::WEB &&
                             status.webSessionId == webSessionId);
  if (!allowed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "The current state does not allow that action.");
  }
  WebCommand command;
  command.type = on ? WebCommandType::PADDLE_ON : WebCommandType::PADDLE_OFF;
  command.requestId = self.allocateRequestId();
  command.webSessionId = webSessionId;
  command.controlLeaseId =
      on ? self.allocateControlLeaseId() : status.controlLeaseId;
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::rinseHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  size_t sessionIndex = 0;
  if (!self.authenticate(request, true, &sessionIndex)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!REMOTE_CN9_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote CN9 actuation is disabled in this firmware build.");
  }
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "Rinse can only start from Ready.");
  }
  WebCommand command;
  command.type = WebCommandType::RINSE;
  command.requestId = self.allocateRequestId();
  portENTER_CRITICAL(&self.dataMux_);
  if (sessionIndex < SESSION_COUNT && self.sessions_[sessionIndex].active) {
    command.webSessionId = self.sessions_[sessionIndex].id;
  }
  portEXIT_CRITICAL(&self.dataMux_);
  command.controlLeaseId = self.allocateControlLeaseId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::stopHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!status.activeCycle || !status.relayClosed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "There is no active shot to stop.");
  }
  WebCommand command;
  command.type = WebCommandType::STOP;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::restartHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle and wait for Ready before restarting.");
  }
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::factoryResetHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before restoring factory settings.");
  }

  char body[REQUEST_BODY_CAPACITY] = {};
  char confirmation[32] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "An explicit factory-reset confirmation is required.");
  }
  cJSON *root = cJSON_Parse(body);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "ERASE_ALL_SETTINGS") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "FACTORY_RESET_NOT_CONFIRMED",
                     "The factory reset was not explicitly confirmed.");
  }

  WebCommand command;
  command.type = WebCommandType::FACTORY_RESET;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; no settings were erased.");
  }
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::networkHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Network settings are locked while a cycle is active.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char action[16] = {};
  WebCommand command;
  command.requestId = self.allocateRequestId();
  static const char *const forgetFields[] = {"action"};
  static const char *const saveFields[] = {"action", "ssid", "password",
                                           "open"};
  bool parsed = root != nullptr &&
                jsonString(root, "action", action, sizeof(action), false);
  if (parsed && strcmp(action, "forget") == 0) {
    parsed = jsonHasOnlyUniqueFields(root, forgetFields, 1);
    if (parsed) {
      command.type = WebCommandType::FORGET_NETWORK;
    }
  } else if (parsed && strcmp(action, "save") == 0) {
    command.type = WebCommandType::SAVE_NETWORK;
    parsed = jsonHasOnlyUniqueFields(root, saveFields, 4) &&
             jsonString(root, "ssid", command.ssid, sizeof(command.ssid),
                        false) &&
             jsonString(root, "password", command.password,
                        sizeof(command.password), true) &&
             jsonBoolean(root, "open", command.openNetwork) &&
             validWifiSsid(command.ssid) &&
             validWifiPassword(command.password, command.openNetwork);
  } else {
    parsed = false;
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_NETWORK",
                     "Invalid network SSID or password.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Nothing was saved.");
  }
  memset(command.password, 0, sizeof(command.password));
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::wifiScanStartHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Wi-Fi scanning is available only while Ready.");
  }

  WebCommand command;
  command.type = WebCommandType::START_WIFI_SCAN;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; scan was not started.");
  }
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::wifiScanStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, false)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session.");
  }
  WifiScanSnapshot scan;
  portENTER_CRITICAL(&self.dataMux_);
  scan = self.scan_;
  portEXIT_CRITICAL(&self.dataMux_);

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  char prefix[128] = {};
  snprintf(prefix, sizeof(prefix),
           "{\"state\":\"%s\",\"updatedAtMs\":%lu,\"networks\":[",
           wifiScanStateName(scan.state),
           static_cast<unsigned long>(scan.updatedAtMs));
  if (httpd_resp_send_chunk(request, prefix, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    return ESP_FAIL;
  }
  for (size_t index = 0; index < scan.count; ++index) {
    if (httpd_resp_send_chunk(request, index == 0 ? "{\"ssid\":" :
                                                  ",{\"ssid\":",
                              HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        sendJsonStringChunk(request, scan.networks[index].ssid) != ESP_OK) {
      return ESP_FAIL;
    }
    char suffix[96] = {};
    snprintf(suffix, sizeof(suffix),
             ",\"rssi\":%ld,\"channel\":%u,\"open\":%s}",
             static_cast<long>(scan.networks[index].rssi),
             static_cast<unsigned>(scan.networks[index].channel),
             scan.networks[index].open ? "true" : "false");
    if (httpd_resp_send_chunk(request, suffix, HTTPD_RESP_USE_STRLEN) !=
        ESP_OK) {
      return ESP_FAIL;
    }
  }
  if (httpd_resp_send_chunk(request, "]}", 2) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t ShotStopperNetwork::apPasswordHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The password cannot be changed while a cycle is active.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  char currentPassword[WIFI_PASSWORD_CAPACITY] = {};
  WebCommand command;
  command.type = WebCommandType::CHANGE_AP_PASSWORD;
  command.requestId = self.allocateRequestId();
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  static const char *const fields[] = {"currentPassword", "newPassword"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 2) &&
      jsonString(root, "currentPassword", currentPassword,
                 sizeof(currentPassword), false) &&
      jsonString(root, "newPassword", command.password,
                 sizeof(command.password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  const bool currentValid =
      parsed && verifyAdminPassword(self.settingsCopy(), currentPassword);
  memset(currentPassword, 0, sizeof(currentPassword));
  if (!currentValid || !validAccessPointPassword(command.password)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_AP_PASSWORD",
                     "The current password is incorrect or the new password is not 8–63 characters long.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Nothing was saved.");
  }
  memset(command.password, 0, sizeof(command.password));
  return self.sendAccepted(request, command.requestId);
}

}  // namespace shotstopper
