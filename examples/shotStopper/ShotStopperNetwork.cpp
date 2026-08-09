#include "ShotStopperNetwork.h"

#include "ShotStopperWebAssets.h"

#include <cJSON.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace shotstopper {
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
constexpr const char *STATUS_UNPROCESSABLE = "422 Unprocessable Entity";
constexpr const char *STATUS_UNAVAILABLE = "503 Service Unavailable";

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
    case ConfigValidationError::BREW_CONFIRM:
      return "Brew confirmation must be from 500 to 10,000 ms.";
    case ConfigValidationError::MIN_AUTO_STOP:
      return "Minimum auto-stop must be from 1,000 to 30,000 ms.";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "CN9 limit must be from 5,000 to 50,000 ms.";
    case ConfigValidationError::TIMING_RELATION:
      return "Required: gesture < brew < auto-stop < limit, and rinse duration <= limit.";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "The Bookoo combined command requires automatic tare.";
  }
  return "Invalid configuration.";
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

}  // namespace

ShotStopperNetwork *ShotStopperNetwork::instance_ = nullptr;

bool ShotStopperNetwork::begin(const PersistedSettings &settings,
                               const NetworkBridgeCallbacks &callbacks) {
  if (instance_ != nullptr || callbacks.copyControlStatus == nullptr ||
      callbacks.enqueueWebCommand == nullptr ||
      callbacks.copyDebugEvents == nullptr) {
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
  if (xTaskCreate(taskEntry, "network_manager", 8192, this,
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

NetworkStatusSnapshot ShotStopperNetwork::snapshot() {
  NetworkStatusSnapshot copy;
  portENTER_CRITICAL(&dataMux_);
  copy = status_;
  portEXIT_CRITICAL(&dataMux_);
  return copy;
}

PersistedSettings ShotStopperNetwork::settingsCopy() {
  PersistedSettings copy;
  portENTER_CRITICAL(&dataMux_);
  copy = settings_;
  portEXIT_CRITICAL(&dataMux_);
  return copy;
}

void ShotStopperNetwork::taskEntry(void *parameter) {
  static_cast<ShotStopperNetwork *>(parameter)->taskLoop();
}

void ShotStopperNetwork::taskLoop() {
  for (;;) {
    service();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void ShotStopperNetwork::service() {
  const uint32_t now = millis();
  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);

  if (!startupComplete_ && controlAllowsConfiguration(control)) {
    startNetwork();
  }
  if (!startupComplete_) {
    return;
  }

  processAcceptedCommands();
  serviceStaState(now);
  serviceWifiScan(now);
  serviceSessions(now);

  const NetworkStatusSnapshot network = snapshot();
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
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    stopHttpServer();
    WiFi.softAPdisconnect(true);
    const PersistedSettings settings = settingsCopy();
    const IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    const bool apReady = WiFi.softAP(AP_SSID, settings.apPassword);
    networkStartedAtMs_ = now;
    networkShutdownPending_ = false;
    portENTER_CRITICAL(&dataMux_);
    scanRequested_ = false;
    scan_ = WifiScanSnapshot{};
    everAuthenticated_ = false;
    lastAuthenticatedAtMs_ = now;
    portEXIT_CRITICAL(&dataMux_);
    const bool httpReady = apReady && startHttpServer();
    portENTER_CRITICAL(&dataMux_);
    status_.apActive = apReady;
    status_.networkActive = apReady && httpReady;
    status_.uiAuthenticated = false;
    status_.apClients = 0;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::NETWORK, DebugCode::AP_STARTED, apReady, httpReady);
  }

  if (restartPending_ && safeForNetworkChange &&
      static_cast<uint32_t>(now - restartRequestedAtMs_) >= RESTART_DELAY_MS) {
    ESP.restart();
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

void ShotStopperNetwork::startNetwork() {
  const PersistedSettings settings = settingsCopy();
  const uint32_t now = millis();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  portENTER_CRITICAL(&dataMux_);
  status_.wifiConfigured = settings.staConfigured;
  strncpy(status_.apIp, AP_IP, sizeof(status_.apIp) - 1);
  portEXIT_CRITICAL(&dataMux_);
  startupComplete_ = true;
  if (settings.staConfigured) {
    startStation(settings, now);
  } else {
    startFallbackAccessPoint(now);
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
      staReconnectAttemptAtMs_ = now;
      Serial.println("WiFi STA disconnected; Web server waiting for reconnect");
      return;
    }
    if (server_ == nullptr && controlAllowsNetworkMutation()) {
      const bool httpReady = startHttpServer();
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
    const bool httpReady = controlAllowsNetworkMutation() && startHttpServer();
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
    startFallbackAccessPoint(now);
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
  const bool safe = controlAllowsConfiguration(control);

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
}

void ShotStopperNetwork::serviceSessions(uint32_t now) {
  bool anyAuthenticated = false;
  uint32_t newestHeartbeat = 0;
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
    if (static_cast<int32_t>(session.lastHeartbeatMs - newestHeartbeat) > 0) {
      newestHeartbeat = session.lastHeartbeatMs;
    }
  }
  status_.uiAuthenticated = anyAuthenticated;
  portEXIT_CRITICAL(&dataMux_);
  if (expiredSessions > 0) {
    log(DebugCategory::WEB, DebugCode::UI_EXPIRED, expiredSessions);
  }

  ControlStatusSnapshot control;
  callbacks_.copyControlStatus(control);
  if (control.activeCycle && control.source == ControlSource::WEB &&
      control.relayClosed &&
      (!anyAuthenticated ||
       static_cast<uint32_t>(now - newestHeartbeat) >=
           WEB_PADDLE_HEARTBEAT_TIMEOUT_MS)) {
    if (!heartbeatStopSent_) {
      WebCommand stop;
      stop.type = WebCommandType::STOP_HEARTBEAT;
      stop.requestId = now;
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
  if (!controlAllowsNetworkMutation() || acceptedCommandQueue_ == nullptr) {
    return;
  }
  WebCommand command;
  size_t processed = 0;
  while (processed < WEB_COMMAND_QUEUE_LENGTH &&
         xQueueReceive(acceptedCommandQueue_, &command, 0) == pdTRUE) {
    if (!controlAllowsNetworkMutation()) {
      xQueueSendToFront(acceptedCommandQueue_, &command, 0);
      return;
    }
    processAcceptedCommand(command);
    ++processed;
  }
}

void ShotStopperNetwork::processAcceptedCommand(const WebCommand &command) {
  PersistedSettings next = settingsCopy();
  bool persist = false;
  bool authenticationChanged = false;
  switch (command.type) {
    case WebCommandType::PERSIST_RUNTIME:
      if (validateRuntimeConfig(command.config) != ConfigValidationError::NONE) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return;
      }
      next.runtime = command.config;
      persist = true;
      break;

    case WebCommandType::SAVE_NETWORK:
      if (!validWifiSsid(command.ssid) ||
          !validWifiPassword(command.password, command.openNetwork)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return;
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
        return;
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
      if (!refreshAuthentication(next, DEFAULT_AP_PASSWORD)) {
        return;
      }
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::NETWORK_RESET);
      break;

    case WebCommandType::RESTART:
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::RESTART_REQUESTED);
      break;

    default:
      return;
  }

  if (persist) {
    if (!savePersistedSettings(next)) {
      restartPending_ = false;
      apRestartPending_ = false;
      log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
      return;
    }
    portENTER_CRITICAL(&dataMux_);
    settings_ = next;
    status_.wifiConfigured = next.staConfigured;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  }

  if (restartPending_ || apRestartPending_ || authenticationChanged) {
    restartRequestedAtMs_ = millis();
    invalidateAllSessions();
  }
}

void ShotStopperNetwork::log(DebugCategory category, DebugCode code,
                             int32_t argument1, int32_t argument2) {
  if (callbacks_.addDebugEvent != nullptr) {
    callbacks_.addDebugEvent(category, code, argument1, argument2);
  }
}

bool ShotStopperNetwork::startHttpServer() {
  if (server_ != nullptr) {
    return true;
  }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = tskIDLE_PRIORITY + 1;
  config.stack_size = 8192;
  config.max_open_sockets = 2;
  config.max_uri_handlers = 16;
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
    WebSession &session = sessions_[sessionIndex];
    session = WebSession{};
    session.active = true;
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
  portENTER_CRITICAL(&dataMux_);
  sessions_[index] = WebSession{};
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::WEB, code);
}

void ShotStopperNetwork::invalidateAllSessions() {
  portENTER_CRITICAL(&dataMux_);
  for (WebSession &session : sessions_) {
    session = WebSession{};
  }
  status_.uiAuthenticated = false;
  portEXIT_CRITICAL(&dataMux_);
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
                     "Demasiados intentos; espere un minuto.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "JSON requerido y acotado.");
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
                     "Clave incorrecta.");
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
  if (!self.authenticate(request, false)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session.");
  }
  ControlStatusSnapshot control;
  self.callbacks_.copyControlStatus(control);
  const NetworkStatusSnapshot network = self.snapshot();
  char currentWeight[32] = "null";
  char lastWeight[32] = "null";
  if (control.currentWeightValid) {
    snprintf(currentWeight, sizeof(currentWeight), "%.2f",
             static_cast<double>(control.currentWeightG));
  }
  if (control.lastCycle.weightValid) {
    snprintf(lastWeight, sizeof(lastWeight), "%.2f",
             static_cast<double>(control.lastCycle.lastWeightG));
  }

  char response[3072] = {};
  const int written = snprintf(
      response, sizeof(response),
      "{\"state\":\"%s\",\"stateLabel\":\"%s\",\"relayClosed\":%s,"
      "\"physicalPaddleOn\":%s,\"virtualPaddleOn\":%s,"
      "\"controlSource\":\"%s\",\"cn9ElapsedMs\":%lu,"
      "\"configMutable\":%s,\"config\":{\"revision\":%lu,"
      "\"goalWeightG\":%u,\"autoTare\":%s,\"timerOnly\":%s,"
      "\"canTareStartTimer\":%s,\"brewConfirmationBeep\":%s,\"rinseGestureMs\":%lu,"
      "\"rinseDurationMs\":%lu,\"brewConfirmMs\":%lu,"
      "\"minAutoStopMs\":%lu,\"operationalWallMs\":%lu},"
      "\"scale\":{\"available\":%s,\"currentWeightG\":%s,"
      "\"weightAgeMs\":%lu},\"lastCycle\":{\"valid\":%s,"
      "\"durationMs\":%lu,\"endReason\":\"%s\","
      "\"lastWeightG\":%s,\"weightAgeMs\":%lu},"
      "\"network\":{\"networkActive\":%s,\"uiActive\":%s,"
      "\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
      "\"wifiConfigured\":%s,\"staState\":\"%s\","
      "\"staIp\":\"%s\",\"windowRemainingMs\":%lu},"
      "\"debugEventsDropped\":%lu}",
      stopperStateName(control.state), stateLabel(control.state),
      control.relayClosed ? "true" : "false",
      control.physicalPaddleOn ? "true" : "false",
      control.virtualPaddleOn ? "true" : "false",
      controlSourceName(control.source),
      static_cast<unsigned long>(control.cn9ElapsedMs),
      controlAllowsConfiguration(control) ? "true" : "false",
      static_cast<unsigned long>(control.config.revision),
      static_cast<unsigned>(control.config.goalWeightG),
      control.config.autoTare ? "true" : "false",
      control.config.timerOnly ? "true" : "false",
      control.config.canTareStartTimer ? "true" : "false",
      control.config.brewConfirmationBeep ? "true" : "false",
      static_cast<unsigned long>(control.config.rinseGestureMs),
      static_cast<unsigned long>(control.config.rinseDurationMs),
      static_cast<unsigned long>(control.config.brewConfirmMs),
      static_cast<unsigned long>(control.config.minAutoStopMs),
      static_cast<unsigned long>(control.config.operationalWallMs),
      control.scaleAvailable ? "true" : "false", currentWeight,
      static_cast<unsigned long>(control.currentWeightAgeMs),
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
      static_cast<unsigned long>(control.debugEventsDropped));
  if (written < 0 || static_cast<size_t>(written) >= sizeof(response)) {
    return sendError(request, "500 Internal Server Error", "STATUS_TOO_LARGE",
                     "Status snapshot exceeds its size limit.");
  }
  return sendJson(request, STATUS_OK, response);
}

esp_err_t ShotStopperNetwork::logHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, false)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session.");
  }
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
                   static_cast<int32_t>(WebCommandType::PERSIST_RUNTIME)) {
      snprintf(message, sizeof(message), "%s: %s",
               debugCodeName(events[index].code),
               webCommandTypeName(
                   static_cast<WebCommandType>(events[index].argument1)));
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
  static const char *const fields[] = {
      "goalWeightG", "rinseGestureMs", "rinseDurationMs", "brewConfirmMs",
      "minAutoStopMs", "operationalWallMs", "autoTare", "timerOnly",
      "canTareStartTimer", "brewConfirmationBeep"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 10) &&
      jsonUint8(root, "goalWeightG", candidate.goalWeightG) &&
      jsonUint32(root, "rinseGestureMs", candidate.rinseGestureMs) &&
      jsonUint32(root, "rinseDurationMs", candidate.rinseDurationMs) &&
      jsonUint32(root, "brewConfirmMs", candidate.brewConfirmMs) &&
      jsonUint32(root, "minAutoStopMs", candidate.minAutoStopMs) &&
      jsonUint32(root, "operationalWallMs", candidate.operationalWallMs) &&
      jsonBoolean(root, "autoTare", candidate.autoTare) &&
      jsonBoolean(root, "timerOnly", candidate.timerOnly) &&
      jsonBoolean(root, "canTareStartTimer", candidate.canTareStartTimer) &&
      jsonBoolean(root, "brewConfirmationBeep",
                  candidate.brewConfirmationBeep);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A field is missing or has an invalid type.");
  }
  const ConfigValidationError error = validateRuntimeConfig(candidate);
  if (error != ConfigValidationError::NONE) {
    self.log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
             static_cast<int32_t>(error));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_CONFIG",
                     configValidationMessage(error));
  }
  WebCommand command;
  command.type = WebCommandType::APPLY_CONFIG;
  command.requestId = millis();
  command.config = candidate;
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
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
  command.requestId = millis();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; calibration was not reset.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
}

esp_err_t ShotStopperNetwork::paddleHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "JSON requerido.");
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
  const bool allowed = on ? controlAllowsConfiguration(status)
                          : (status.activeCycle &&
                             status.source == ControlSource::WEB);
  if (!allowed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "The current state does not allow that action.");
  }
  WebCommand command;
  command.type = on ? WebCommandType::PADDLE_ON : WebCommandType::PADDLE_OFF;
  command.requestId = millis();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
}

esp_err_t ShotStopperNetwork::rinseHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authenticate(request, true)) {
    return sendError(request, STATUS_UNAUTHORIZED, "UNAUTHORIZED",
                     "Invalid session or CSRF token.");
  }
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!controlAllowsConfiguration(status)) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "Rinse can only start from Ready.");
  }
  WebCommand command;
  command.type = WebCommandType::RINSE;
  command.requestId = millis();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
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
  command.requestId = millis();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
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
  command.requestId = millis();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
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
  command.requestId = millis();
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
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
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

  portENTER_CRITICAL(&self.dataMux_);
  if (self.scan_.state != WifiScanState::RUNNING &&
      self.scan_.state != WifiScanState::QUEUED) {
    self.scan_ = WifiScanSnapshot{};
    self.scan_.state = WifiScanState::QUEUED;
    self.scan_.updatedAtMs = millis();
    self.scanRequested_ = true;
  }
  portEXIT_CRITICAL(&self.dataMux_);
  return sendJson(request, STATUS_ACCEPTED,
                  "{\"accepted\":true,\"state\":\"QUEUED\"}");
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
  command.requestId = millis();
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
  return sendJson(request, STATUS_ACCEPTED, "{\"accepted\":true}");
}

}  // namespace shotstopper
