#include "ShotStopperNetwork.h"
#include "ShotStopperPsram.h"
#include "ShotStopperSerialCli.h"
#include "ShotStopperVersion.h"
#include "ShotStopperWatchdog.h"

#include "ShotStopperWebAssetsGzip.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace shotstopper {

WallClock g_wallClock;

struct NetworkWorkBuf {
  static constexpr size_t kStatusJson = 7680;
  static constexpr size_t kPresetsJson = 2800;
  static constexpr size_t kHistoryJson = 1400;
  static constexpr size_t kJsonItem = 768;
  char statusJson[kStatusJson];
  char presetsJson[kPresetsJson];
  char historyJson[kHistoryJson];
  char jsonItem[kJsonItem];
  DebugEvent logBatch[48];
  ShotLogRecord shotRecords[SHOT_LOG_CAPACITY];
  ControlStatusSnapshot control;
};

namespace {

NetworkWorkBuf *g_work = nullptr;

constexpr const char *AP_SSID = "MicraShotStopperAP";
constexpr const char *AP_IP = "192.168.4.1";
constexpr const char *JSON_CONTENT_TYPE = "application/json";
constexpr const char *STATUS_OK = "200 OK";
constexpr const char *STATUS_NOT_MODIFIED = "304 Not Modified";
constexpr const char *STATUS_ACCEPTED = "202 Accepted";
constexpr size_t WEB_UI_ETAG_CAPACITY = 64;
constexpr size_t IF_NONE_MATCH_CAPACITY = 80;
constexpr const char *STATUS_BAD_REQUEST = "400 Bad Request";
constexpr const char *STATUS_UNAUTHORIZED = "401 Unauthorized";
constexpr const char *STATUS_TOO_MANY = "429 Too Many Requests";
constexpr const char *STATUS_CONFLICT = "409 Conflict";
constexpr const char *STATUS_NOT_FOUND = "404 Not Found";
constexpr const char *STATUS_UNPROCESSABLE = "422 Unprocessable Entity";
constexpr const char *STATUS_UNAVAILABLE = "503 Service Unavailable";
// PersistedSettings is ≤ PERSISTED_SETTINGS_NVS_BUDGET (3072 B).
// processPersistedCommand() keeps one copy on the stack, and its callees
// (settingsCopy, resetPersistedSettingsToFactory, savePersistedSettings) add
// further frames.  7 168 was too small and triggered a stack-canary watchpoint
// crash on FACTORY_RESET via the serial CLI.  12 288 gives comfortable headroom.
constexpr uint32_t NETWORK_MANAGER_TASK_STACK_SIZE = 12288;
constexpr uint32_t HTTP_SERVER_TASK_STACK_SIZE = 10240;

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

bool jsonFieldPresent(cJSON *object, const char *name) {
  return object != nullptr && name != nullptr &&
         cJSON_GetObjectItemCaseSensitive(object, name) != nullptr;
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
  // uint64_t bitmask: config payloads can exceed 32 fields (e.g. alerts +
  // output channel). Keep the ceiling explicit so callers fail loudly if they
  // outgrow the mask again.
  if (!cJSON_IsObject(object) || allowed == nullptr || allowedCount > 64) {
    return false;
  }
  uint64_t seen = 0;
  for (cJSON *item = object->child; item != nullptr; item = item->next) {
    if (item->string == nullptr) {
      return false;
    }
    size_t index = 0;
    while (index < allowedCount && strcmp(item->string, allowed[index]) != 0) {
      ++index;
    }
    if (index == allowedCount || (seen & (uint64_t{1} << index)) != 0) {
      return false;
    }
    seen |= uint64_t{1} << index;
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
      return "Rinse gesture must be from 0.1 to 5 s.";
    case ConfigValidationError::RINSE_DURATION:
      return "Rinse duration must be from 0.5 to 10 s.";
    case ConfigValidationError::RETARE_WINDOW:
      return "Retare window must be from 0.5 to 10 s.";
    case ConfigValidationError::MINIMUM_CUP_WEIGHT:
      return "Minimum cup weight must be from 1 to 500 g.";
    case ConfigValidationError::RETARE_STABILITY_SAMPLES:
      return "Retare stable samples must be from 2 to 10.";
    case ConfigValidationError::RETARE_STABILITY_TOLERANCE:
      return "Retare stability tolerance must be from 0.1 to 20 g.";
    case ConfigValidationError::RETARE_STABILITY_MAX_GAP:
      return "Retare sample gap must be from 0.1 to 5 s.";
    case ConfigValidationError::RETARE_STABILITY_MIN_DURATION:
      return "Retare min stable time must be from 0 to 2 s.";
    case ConfigValidationError::RETARE_STABILITY_RELATION:
      return "Retare min stable time must be ≤ retare window and ≤ "
             "samples × sample gap.";
    case ConfigValidationError::BBW_PROTECTION_TIMEOUT:
      return "BBW protection must be from 0.5 to 30 s.";
    case ConfigValidationError::BBW_PROTECTION_RETARE_RELATION:
      return "BBW protection must be at least effective retare "
             "window + 3 s.";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "Max BBW time must be from 5 to 60 s.";
    case ConfigValidationError::PADDLE_REMINDER_INTERVAL:
      return "Paddle reminder interval must be from 5 to 60 s.";
    case ConfigValidationError::PADDLE_REMINDER_MAX_DURATION:
      return "Paddle reminder limit must be from 1 to 60 min and at least "
             "the reminder interval.";
    case ConfigValidationError::TIMING_RELATION:
      return "Required: rinse gesture < Max BBW time; rinse duration, retare "
             "window, and BBW protection each ≤ Max BBW time.";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "The Bookoo combined command requires automatic tare.";
    case ConfigValidationError::SCALE_TIMER_STOP_EXTRA_DELAY:
      return "Scale timer stop extra delay must be from 0 to 1000 ms.";
    case ConfigValidationError::TIMEZONE_OFFSET:
      return "Timezone offset must be from -720 to +840 minutes.";
    case ConfigValidationError::NTP_SERVER_PRESET:
      return "NTP server preset must be pool, google, cloudflare, or nist.";
    case ConfigValidationError::NTP_SERVER_CUSTOM:
      return "Custom NTP hostname is invalid (letters, digits, '.', '-'; "
             "max 63 chars).";
    case ConfigValidationError::MAX_RECOVERY_WEIGHT:
      return "Max recovery must be from 10 to 200 g.";
    case ConfigValidationError::MIN_BREW_TIME:
      return "Min brew time must be from 5 to 55 s.";
    case ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION:
      return "Fast guard requires max recovery > target, min brew < Max BBW "
             "time, and min brew ≥ BBW protection.";
    case ConfigValidationError::MIN_RECOVERY_WEIGHT:
      return "Min recovery must be from 10 to 200 g.";
    case ConfigValidationError::MAX_BREW_TIME:
      return "Max brew time must be from 5 to 55 s.";
    case ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION:
      return "Slow guard requires min recovery < target, max brew < Max BBW "
             "time, max brew ≥ BBW protection, and max brew > min brew "
             "when Fast is on.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE:
      return "A→M limit mode must be manual or auto.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT:
      return "A→M manual limit must be from 10 s up to Max BBW time.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE:
      return "A→M baseline must be from 10 s up to Max BBW time.";
    case ConfigValidationError::WEIGHT_OFFSET_BASELINE:
      return "Offset baseline must be from 0 to 5.0 g.";
    case ConfigValidationError::SCALE_MAC_CACHE_MODE:
      return "Always use this scale must be on or off.";
    case ConfigValidationError::ALERT_OUTPUT_CHANNEL:
      return "Alert output channel must be scale_only, buzzer_only, or "
             "scale_priority.";
    case ConfigValidationError::EXTENDED_PULSE_RATE:
      return "Extended shot pulse must be disabled, slow, medium, fast, or "
             "rapid.";
    case ConfigValidationError::SLOW_EXTENDED_PULSE_RATE:
      return "Slow extended pulse must be disabled, slow, medium, fast, or "
             "rapid.";
    case ConfigValidationError::BOOKOO_CONNECT_BEEP_LEVEL:
      return "Bookoo scale volume must be disabled (0) or 1 to 5.";
    case ConfigValidationError::LAST_SHOT_COOLDOWN:
      return "Last shot cooldown must be from 5 to 240 min.";
    case ConfigValidationError::DRIP_DELAY:
      return "Drip delay must be from 0 to 10 s.";
    case ConfigValidationError::RING_RETAIN_LOG_LEVEL:
      return "Ring log level must be none, critical, error, warning, info, or "
             "debug.";
    case ConfigValidationError::PADDLE_MODE:
      return "Paddle mode must be auto, natural or original.";
  }
  return "Invalid configuration.";
}

bool jsonAutoToManualGuardLimitMode(cJSON *object, const char *name,
                                    uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "manual") == 0) {
    output = static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
    return true;
  }
  if (strcmp(item->valuestring, "auto") == 0) {
    output = static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
    return true;
  }
  return false;
}

const char *autoToManualGuardLimitModeId(uint8_t mode) {
  return mode == static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL)
             ? "manual"
             : "auto";
}

bool jsonPaddleMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parsePaddleMode(item->valuestring, output);
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

bool jsonScaleMacCacheMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseScaleMacCacheMode(item->valuestring, output);
}

bool jsonAlertOutputChannel(cJSON *object, const char *name, uint8_t &output,
                            bool optional) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (item == nullptr) {
    return optional;
  }
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseAlertOutputChannel(item->valuestring, output);
}

bool jsonLogLevel(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseLogLevel(item->valuestring, output);
}

bool jsonExtendedPulseRate(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseExtendedPulseRate(item->valuestring, output);
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


enum class StatusPage : uint8_t { Home, Settings, Admin, Diagnostic, Unknown };

StatusPage parseStatusPage(const char *uri) {
  if (uri == nullptr) {
    return StatusPage::Unknown;
  }
  const char *path = uri;
  const char *query = strchr(uri, '?');
  char stack[64] = {};
  if (query != nullptr) {
    const size_t len = static_cast<size_t>(query - uri);
    if (len >= sizeof(stack)) {
      return StatusPage::Unknown;
    }
    memcpy(stack, uri, len);
    stack[len] = '\0';
    path = stack;
  }
  if (strcmp(path, "/api/v1/status/home") == 0) {
    return StatusPage::Home;
  }
  if (strcmp(path, "/api/v1/status/settings") == 0) {
    return StatusPage::Settings;
  }
  if (strcmp(path, "/api/v1/status/admin") == 0) {
    return StatusPage::Admin;
  }
  if (strcmp(path, "/api/v1/status/diagnostic") == 0) {
    return StatusPage::Diagnostic;
  }
  return StatusPage::Unknown;
}

bool statusJsonAppend(size_t *used, const char *fmt, ...) {
  if (g_work == nullptr || used == nullptr ||
      *used >= NetworkWorkBuf::kStatusJson) {
    return false;
  }
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(g_work->statusJson + *used,
                          NetworkWorkBuf::kStatusJson - *used, fmt, args);
  va_end(args);
  if (n < 0 ||
      static_cast<size_t>(n) >= NetworkWorkBuf::kStatusJson - *used) {
    return false;
  }
  *used += static_cast<size_t>(n);
  return true;
}

void buildSlimPresetsJson(const ControlStatusSnapshot &control) {
  if (g_work == nullptr) {
    return;
  }
  char *buf = g_work->presetsJson;
  const size_t cap = NetworkWorkBuf::kPresetsJson;
  buf[0] = '{';
  buf[1] = '}';
  buf[2] = 0;
  size_t used = 0;
  int n = snprintf(buf, cap, "{\"activeId\":%u,\"items\":[",
                   static_cast<unsigned>(control.presets.activeId));
  if (n > 0) {
    used = static_cast<size_t>(n);
  }
  for (uint8_t i = 0; i < control.presets.count && i < MAX_SHOT_PRESETS; ++i) {
    const ShotPreset &p = control.presets.presets[i];
    char safeName[SHOT_PRESET_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(p.name, safeName, sizeof(safeName));
    n = snprintf(
        buf + used, cap - used,
        "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s,\"brewByWeight\":%s,"
        "\"goalWeightG\":%u,\"minBrewTimeMs\":%lu,\"maxRecoveryWeightG\":%.1f}",
        i == 0 ? "" : ",", static_cast<unsigned>(p.id), safeName,
        p.isFactory ? "true" : "false", p.brewByWeight ? "true" : "false",
        static_cast<unsigned>(p.goalWeightG),
        static_cast<unsigned long>(p.minBrewTimeMs),
        static_cast<double>(p.maxRecoveryWeightG));
    if (n < 0 || static_cast<size_t>(n) >= cap - used) {
      break;
    }
    used += static_cast<size_t>(n);
  }
  if (used + 2 < cap) {
    buf[used++] = ']';
    buf[used++] = '}';
    buf[used] = 0;
  }
}

void buildScaleHistoryJson(const ControlStatusSnapshot &control) {
  if (g_work == nullptr) {
    return;
  }
  char *buf = g_work->historyJson;
  const size_t cap = NetworkWorkBuf::kHistoryJson;
  buf[0] = '[';
  buf[1] = ']';
  buf[2] = 0;
  size_t used = 0;
  int n = snprintf(buf, cap, "[");
  if (n > 0) {
    used = static_cast<size_t>(n);
  }
  bool first = true;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    const ScaleHistoryEntry &entry = control.scaleHistory[i];
    if (entry.mac[0] == '\0') {
      continue;
    }
    char safeMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
    char safeName[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(entry.mac, safeMac, sizeof(safeMac));
    sanitizeJsonEmbed(entry.name, safeName, sizeof(safeName));
    n = snprintf(buf + used, cap - used, "%s{\"mac\":\"%s\",\"name\":\"%s\"}",
                 first ? "" : ",", safeMac, safeName);
    if (n <= 0 || static_cast<size_t>(n) >= cap - used) {
      break;
    }
    used += static_cast<size_t>(n);
    first = false;
  }
  if (used + 1 < cap) {
    buf[used++] = ']';
    buf[used] = 0;
  }
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
  statusResponseMux_ = xSemaphoreCreateMutex();
  if (statusResponseMux_ == nullptr) {
    vQueueDelete(acceptedCommandQueue_);
    acceptedCommandQueue_ = nullptr;
    return false;
  }
  workBuf_ = static_cast<NetworkWorkBuf *>(
      allocExternalOrInternal(sizeof(NetworkWorkBuf)));
  if (workBuf_ != nullptr) {
    memset(workBuf_, 0, sizeof(*workBuf_));
  }
  g_work = workBuf_;
  instance_ = this;
  ntpConfigRevision_ = settings_.runtime.revision;
  g_wallClock.reset();
  // Pin beside the Wi-Fi/LwIP stacks on PRO_CPU (core 0).
  if (xTaskCreatePinnedToCore(taskEntry, "network_manager",
                              NETWORK_MANAGER_TASK_STACK_SIZE, this,
                              tskIDLE_PRIORITY + 1, &taskHandle_,
                              0) != pdPASS) {
    instance_ = nullptr;
    g_work = nullptr;
    heapCapsFree(workBuf_);
    workBuf_ = nullptr;
    vSemaphoreDelete(statusResponseMux_);
    statusResponseMux_ = nullptr;
    vQueueDelete(acceptedCommandQueue_);
    acceptedCommandQueue_ = nullptr;
    taskHandle_ = nullptr;
    return false;
  }
  return true;
}

bool ShotStopperNetwork::lockWorkBuf() {
  if (workBuf_ == nullptr || statusResponseMux_ == nullptr) {
    return false;
  }
  return xSemaphoreTake(statusResponseMux_, pdMS_TO_TICKS(1500)) == pdTRUE;
}

void ShotStopperNetwork::unlockWorkBuf() {
  xSemaphoreGive(statusResponseMux_);
}

esp_err_t ShotStopperNetwork::workBufBusy(httpd_req_t *request) {
  return sendError(request, STATUS_UNAVAILABLE, "STATUS_BUSY",
                   "Status snapshot is busy; retry shortly.");
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
  const bool pendingConfirm =
      staConfirmArmed_ &&
      settings_.staConfigState ==
          static_cast<uint8_t>(StaConfigState::PENDING) &&
      !status_.apActive;
  const uint32_t deadline = staConfirmDeadlineMs_;
  portEXIT_CRITICAL(&dataMux_);
  copy.taskAgeMs = static_cast<uint32_t>(millis() - lastTaskProgressAtMs_);
  copy.taskStackMinWords = taskStackMinWords_;
  if (pendingConfirm) {
    const uint32_t now = millis();
    copy.confirmRemainingMs =
        static_cast<int32_t>(deadline - now) > 0 ? (deadline - now) : 0;
  } else {
    copy.confirmRemainingMs = 0;
  }
  return copy;
}

PersistedSettings ShotStopperNetwork::settingsCopy() {
  PersistedSettings copy;
  portENTER_CRITICAL(&dataMux_);
  copy = settings_;
  portEXIT_CRITICAL(&dataMux_);
  return copy;
}

void ShotStopperNetwork::mergePreferredScaleMac(PersistedSettings &settings) {
  if (callbacks_.copyPreferredScaleMac != nullptr) {
    callbacks_.copyPreferredScaleMac(settings.preferredScaleMac,
                                     sizeof(settings.preferredScaleMac));
  }
  if (callbacks_.copyPreferredScaleName != nullptr) {
    callbacks_.copyPreferredScaleName(settings.preferredScaleName,
                                      sizeof(settings.preferredScaleName));
  }
  if (callbacks_.copyScaleHistory != nullptr) {
    callbacks_.copyScaleHistory(settings.scaleHistory);
  }
}

void ShotStopperNetwork::overlayLiveShotSettings(PersistedSettings &settings) {
  RuntimeConfig liveRuntime = settings.runtime;
  ShotPresetBank livePresets = settings.presets;
  if (callbacks_.copyRuntimeConfig != nullptr) {
    callbacks_.copyRuntimeConfig(&liveRuntime);
  }
  if (callbacks_.copyPresetBank != nullptr) {
    callbacks_.copyPresetBank(&livePresets);
  }
  overlayLivePersistedSettings(settings, liveRuntime, livePresets);
  mergePreferredScaleMac(settings);
}

void ShotStopperNetwork::syncPreferredScaleMac(const char *mac) {
  syncPreferredScale(mac, nullptr);
}

void ShotStopperNetwork::syncPreferredScale(const char *mac, const char *name) {
  if (mac == nullptr || !validPreferredScaleMac(mac)) {
    return;
  }
  char safeName[PREFERRED_SCALE_NAME_CAPACITY] = {};
  if (name != nullptr && validPreferredScaleName(name)) {
    strncpy(safeName, name, sizeof(safeName) - 1);
  }
  portENTER_CRITICAL(&dataMux_);
  strncpy(settings_.preferredScaleMac, mac,
          sizeof(settings_.preferredScaleMac) - 1);
  settings_.preferredScaleMac[sizeof(settings_.preferredScaleMac) - 1] = '\0';
  strncpy(settings_.preferredScaleName, safeName,
          sizeof(settings_.preferredScaleName) - 1);
  settings_.preferredScaleName[sizeof(settings_.preferredScaleName) - 1] = '\0';
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::syncLiveRuntime(const RuntimeConfig &runtime,
                                         const ShotPresetBank *presets) {
  portENTER_CRITICAL(&dataMux_);
  settings_.runtime = runtime;
  if (presets != nullptr) {
    settings_.presets = *presets;
  }
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::syncDurableStorageRevision(uint32_t storageRevision) {
  portENTER_CRITICAL(&dataMux_);
  settings_.storageRevision = storageRevision;
  portEXIT_CRITICAL(&dataMux_);
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

  // Drain CLI link mutations even when SoftAP/HTTP startup is still failing.
  // A successful AP_START / WIFI_CONNECT can mark startup complete and skip
  // the automatic retry that would otherwise fight the user's command.
  processAcceptedCommands();

  if (!startupComplete_ &&
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
  bool confirmRequested = false;
  portENTER_CRITICAL(&dataMux_);
  confirmRequested = pendingConfirmRequest_;
  if (confirmRequested) {
    pendingConfirmRequest_ = false;
  }
  portEXIT_CRITICAL(&dataMux_);
  if (confirmRequested) {
    confirmPendingNetwork("public WebUI request");
  }
  serviceStaState(now);
  serviceWifiScan(now);

  const NetworkStatusSnapshot networkSnapshot = snapshot();
  const bool staConnected =
      networkSnapshot.staState == StaState::CONNECTED &&
      networkSnapshot.wifiConfigured && WiFi.status() == WL_CONNECTED &&
      networkSnapshot.staIp[0] != '\0';
  serviceNtp(now, staConnected);

  const uint8_t apClients = networkSnapshot.apActive
                                ? static_cast<uint8_t>(
                                      WiFi.softAPgetStationNum())
                                : 0;
  portENTER_CRITICAL(&dataMux_);
  status_.apClients = apClients;
  portEXIT_CRITICAL(&dataMux_);
  refreshExtendedStatus(now);

  // Copy the large control snapshot only after command processing has
  // returned. Keeping it in this frame while httpd_stop() enters LwIP used to
  // consume another ~1.8 KiB of the network task stack.
  const bool safeForNetworkChange = controlAllowsNetworkMutation();
  if (apRestartPending_ && safeForNetworkChange &&
      static_cast<uint32_t>(now - restartRequestedAtMs_) >= RESTART_DELAY_MS) {
    apRestartPending_ = false;
    if (ensureAccessPoint(now, true)) {
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
  publishConfiguredAddressStatus();
  if (settings.staConfigured) {
    startStation(settings, now);
    startupComplete_ = true;
    return true;
  } else {
    startupComplete_ = ensureAccessPoint(now);
    return startupComplete_;
  }
}

void ShotStopperNetwork::publishConfiguredAddressStatus() {
  const PersistedSettings settings = settingsCopy();
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char configuredIp[16] = {};
  char configuredNetmask[16] = {};
  char configuredGateway[16] = {};
  char configuredDns1[16] = {};
  char configuredDns2[16] = {};
  if (settings.staConfigured) {
    strncpy(staSsid, settings.staSsid, sizeof(staSsid) - 1);
  }
  if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    formatIpv4(settings.staIp, configuredIp);
    formatIpv4(settings.staNetmask, configuredNetmask);
    formatIpv4(settings.staGateway, configuredGateway);
    formatIpv4(settings.staDns1, configuredDns1);
    if (!ipv4IsZero(settings.staDns2)) {
      formatIpv4(settings.staDns2, configuredDns2);
    }
  }
  portENTER_CRITICAL(&dataMux_);
  status_.staOpen = settings.staConfigured && settings.staOpen;
  status_.staIpMode = settings.staIpMode;
  status_.staConfigState = settings.staConfigState;
  memset(status_.staSsid, 0, sizeof(status_.staSsid));
  strncpy(status_.staSsid, staSsid, sizeof(status_.staSsid) - 1);
  strncpy(status_.configuredIp, configuredIp, sizeof(status_.configuredIp) - 1);
  strncpy(status_.configuredNetmask, configuredNetmask,
          sizeof(status_.configuredNetmask) - 1);
  strncpy(status_.configuredGateway, configuredGateway,
          sizeof(status_.configuredGateway) - 1);
  strncpy(status_.configuredDns1, configuredDns1,
          sizeof(status_.configuredDns1) - 1);
  strncpy(status_.configuredDns2, configuredDns2,
          sizeof(status_.configuredDns2) - 1);
  portEXIT_CRITICAL(&dataMux_);
}

bool ShotStopperNetwork::serialDebugEnabled() const {
  if (callbacks_.copyRuntimeConfig == nullptr) {
    return false;
  }
  RuntimeConfig live = {};
  callbacks_.copyRuntimeConfig(&live);
  return live.serialDebugOutput;
}

void ShotStopperNetwork::armPendingConfirmWindow(uint32_t now) {
  portENTER_CRITICAL(&dataMux_);
  staConfirmArmed_ = true;
  staConfirmDeadlineMs_ = now + STA_CONFIRM_TIMEOUT_MS;
  portEXIT_CRITICAL(&dataMux_);
  if (serialDebugEnabled()) {
    Serial.println("WiFi STA config pending confirmation for 180 s");
  }
}

void ShotStopperNetwork::clearPendingConfirmWindow() {
  portENTER_CRITICAL(&dataMux_);
  staConfirmArmed_ = false;
  staConfirmDeadlineMs_ = 0;
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::requestPendingNetworkConfirm() {
  portENTER_CRITICAL(&dataMux_);
  const bool pending =
      settings_.staConfigState ==
          static_cast<uint8_t>(StaConfigState::PENDING) &&
      settings_.staConfigured && !status_.apActive;
  if (pending) {
    pendingConfirmRequest_ = true;
  }
  portEXIT_CRITICAL(&dataMux_);
}

bool ShotStopperNetwork::confirmPendingNetwork(const char *reason) {
  PersistedSettings next = settingsCopy();
  NetworkStatusSnapshot status = snapshot();
  if (next.staConfigState != static_cast<uint8_t>(StaConfigState::PENDING) ||
      !next.staConfigured || status.apActive ||
      status.staState != StaState::CONNECTED) {
    return false;
  }
  next.staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  copyActiveStaToLkg(next);
  overlayLiveShotSettings(next);
  if (!savePersistedSettings(next)) {
    return false;
  }
  portENTER_CRITICAL(&dataMux_);
  settings_ = next;
  status_.staConfigState = next.staConfigState;
  pendingConfirmRequest_ = false;
  portEXIT_CRITICAL(&dataMux_);
  clearPendingConfirmWindow();
  publishConfiguredAddressStatus();
  log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
      static_cast<int32_t>(next.runtime.revision));
  if (serialDebugEnabled()) {
    Serial.print("WiFi STA config confirmed");
    if (reason != nullptr && reason[0] != '\0') {
      Serial.print(" (");
      Serial.print(reason);
      Serial.print(')');
    }
    Serial.println();
  }
  return true;
}

bool ShotStopperNetwork::revertPendingNetwork(uint32_t now,
                                              const char *reason) {
  PersistedSettings next = settingsCopy();
  if (next.staConfigState != static_cast<uint8_t>(StaConfigState::PENDING)) {
    return false;
  }
  if (serialDebugEnabled()) {
    Serial.print("WiFi STA pending config reverted");
    if (reason != nullptr && reason[0] != '\0') {
      Serial.print(" (");
      Serial.print(reason);
      Serial.print(')');
    }
    Serial.println();
  }
  if (!restoreLkgToActive(next)) {
    clearStaNetwork(next);
  }
  overlayLiveShotSettings(next);
  if (!savePersistedSettings(next)) {
    return false;
  }
  portENTER_CRITICAL(&dataMux_);
  settings_ = next;
  status_.wifiConfigured = next.staConfigured;
  status_.staConfigState = next.staConfigState;
  portEXIT_CRITICAL(&dataMux_);
  clearPendingConfirmWindow();
  publishConfiguredAddressStatus();
  log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
      static_cast<int32_t>(next.runtime.revision));
  stopNtp();
  staNtpEligibleAtMs_ = 0;
  g_wallClock.markDisabled();
  // Keep staEverConnected_: SoftAP auto-raise stays boot-only after any prior
  // successful STA join this process (AP_START / reboot still available).
  if (next.staConfigured) {
    startStation(next, now);
    return true;
  }
  if (staEverConnected_) {
    stopHttpServer();
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    portENTER_CRITICAL(&dataMux_);
    status_.networkActive = false;
    status_.apActive = false;
    status_.apClients = 0;
    status_.staState = StaState::NOT_CONFIGURED;
    status_.staIp[0] = '\0';
    status_.staLinkMetricsValid = false;
    status_.staRssi = 0;
    status_.staSignalQualityPct = 0;
    portEXIT_CRITICAL(&dataMux_);
    lifecycleLog(
        "WiFi STA cleared after prior connect; SoftAP suppressed (AP_START or reboot)");
    return true;
  }
  return ensureAccessPoint(now);
}

void ShotStopperNetwork::applyStationAddressConfig(
    const PersistedSettings &settings) {
  if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    const IPAddress ip(settings.staIp[0], settings.staIp[1], settings.staIp[2],
                       settings.staIp[3]);
    const IPAddress gateway(settings.staGateway[0], settings.staGateway[1],
                            settings.staGateway[2], settings.staGateway[3]);
    const IPAddress netmask(settings.staNetmask[0], settings.staNetmask[1],
                            settings.staNetmask[2], settings.staNetmask[3]);
    const IPAddress dns1(settings.staDns1[0], settings.staDns1[1],
                         settings.staDns1[2], settings.staDns1[3]);
    const IPAddress dns2(settings.staDns2[0], settings.staDns2[1],
                         settings.staDns2[2], settings.staDns2[3]);
    if (!ipv4IsZero(settings.staDns2)) {
      WiFi.config(ip, gateway, netmask, dns1, dns2);
    } else {
      WiFi.config(ip, gateway, netmask, dns1);
    }
  } else {
    const IPAddress none;
    WiFi.config(none, none, none);
  }
}

void ShotStopperNetwork::beginStationConnect(const PersistedSettings &settings,
                                             uint32_t now) {
  const bool apActive = snapshot().apActive;
  if (apActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  applyStationAddressConfig(settings);
  staConnectStartedAtMs_ = now;
  staReconnectAttemptAtMs_ = now;
  portENTER_CRITICAL(&dataMux_);
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  status_.staLinkMetricsValid = false;
  status_.staRssi = 0;
  status_.staSignalQualityPct = 0;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::STA_CONNECTING);
  if (apActive) {
    lifecycleLog(settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)
                     ? "WiFi STA retrying with static IP; SoftAP stays up"
                     : "WiFi STA retrying; SoftAP stays up");
  } else if (settings.staIpMode == static_cast<uint8_t>(StaIpMode::STATIC)) {
    lifecycleLog("WiFi STA connecting with static IP; AP disabled");
  } else {
    lifecycleLog("WiFi STA connecting; AP disabled");
  }
  WiFi.disconnect(false, false);
  WiFi.begin(settings.staSsid,
             settings.staOpen ? nullptr : settings.staPassword);
}

void ShotStopperNetwork::startStation(const PersistedSettings &settings,
                                      uint32_t now) {
  stopHttpServer();
  abortWifiScan(now, false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  // Do not clear staEverConnected_: once STA has joined this boot, SoftAP must
  // not auto-raise again (serial AP_START or reboot only).
  clearPendingConfirmWindow();
  portENTER_CRITICAL(&dataMux_);
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  status_.staLinkMetricsValid = false;
  status_.staRssi = 0;
  status_.staSignalQualityPct = 0;
  status_.windowRemainingMs = 0;
  status_.confirmRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  publishConfiguredAddressStatus();
  beginStationConnect(settings, now);
}

void ShotStopperNetwork::stopSoftAp(bool stopHttp) {
  if (!snapshot().apActive) {
    return;
  }
  // Keep WIFI_AP_STA (or current mode) after SoftAP stop so the fresh STA
  // association is not reset by a mode switch to WIFI_STA.
  WiFi.softAPdisconnect(true);
  if (stopHttp) {
    stopHttpServer();
  }
  portENTER_CRITICAL(&dataMux_);
  status_.apActive = false;
  status_.apClients = 0;
  if (stopHttp) {
    status_.networkActive = false;
  }
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
  lifecycleLog(stopHttp ? "WiFi SoftAP stopped; HTTP stopped"
                        : "WiFi SoftAP stopped; HTTP kept");
}

void ShotStopperNetwork::stopSoftApKeepStation() {
  stopSoftAp(true);
}

void ShotStopperNetwork::stopSoftApLeaveHttp() {
  stopSoftAp(false);
}

bool ShotStopperNetwork::wifiScanInProgress() {
  bool busy = false;
  portENTER_CRITICAL(&dataMux_);
  busy = scanRequested_ || scan_.state == WifiScanState::QUEUED ||
         scan_.state == WifiScanState::RUNNING;
  portEXIT_CRITICAL(&dataMux_);
  return busy;
}

void ShotStopperNetwork::abortWifiScan(uint32_t now, bool logTimeout) {
  bool wasRunning = false;
  bool wasQueuedOrRequested = false;
  portENTER_CRITICAL(&dataMux_);
  wasRunning = scan_.state == WifiScanState::RUNNING;
  wasQueuedOrRequested =
      scanRequested_ || scan_.state == WifiScanState::QUEUED || wasRunning;
  if (wasQueuedOrRequested) {
    scanRequested_ = false;
    scan_.state = logTimeout ? WifiScanState::FAILED : WifiScanState::CANCELED;
    scan_.updatedAtMs = now;
    scan_.count = 0;
  }
  portEXIT_CRITICAL(&dataMux_);
  if (wasRunning) {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
  }
  if (!wasQueuedOrRequested) {
    return;
  }
  log(DebugCategory::NETWORK,
      logTimeout ? DebugCode::WIFI_SCAN_ERROR : DebugCode::WIFI_SCAN_CANCELED,
      logTimeout ? -1 : 0);
  if (scanMaintenanceLeaseId_ != 0) {
    WebCommand command;
    command.type = WebCommandType::START_WIFI_SCAN;
    command.requestId = scanRequestId_;
    command.maintenanceLeaseId = scanMaintenanceLeaseId_;
    (void)enqueueMaintenanceCompletion(command, false,
                                       logTimeout ? CommandResultState::FAILED
                                                  : CommandResultState::CANCELED);
    scanMaintenanceLeaseId_ = 0;
    scanRequestId_ = 0;
  }
}

bool ShotStopperNetwork::ensureAccessPoint(uint32_t now, bool force) {
  const PersistedSettings settings = settingsCopy();
  NetworkStatusSnapshot status = snapshot();
  if (!force && status.apActive && status.networkActive && server_ != nullptr) {
    return true;
  }

  abortWifiScan(now, false);
  const bool keepStation = settings.staConfigured;
  const bool staLinkUp = keepStation && WiFi.status() == WL_CONNECTED;
  const bool wantHttp = !httpStartHeld_;
  const bool keepHttp = wantHttp && staLinkUp && server_ != nullptr;
  if (wantHttp && !keepHttp) {
    stopHttpServer();
  }
  if (!keepStation) {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_AP);
  } else {
    // Preserve any in-flight STA association attempt while SoftAP is raised.
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP_STA);
  }
  const IPAddress ip(192, 168, 4, 1);
  const bool configured =
      WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool apReady = configured && WiFi.softAP(AP_SSID, settings.apPassword);
  const bool httpReady =
      keepHttp || (wantHttp && apReady && startHttpServer());
  networkStartedAtMs_ = now;
  portENTER_CRITICAL(&dataMux_);
  status_.networkActive = apReady && httpReady;
  status_.apActive = apReady;
  status_.apClients = 0;
  if (!settings.staConfigured) {
    status_.staState = StaState::NOT_CONFIGURED;
  } else if (status_.staState == StaState::CONNECTED && !staLinkUp) {
    status_.staState = StaState::DISCONNECTED;
  }
  if (!staLinkUp &&
      (!keepStation || status_.staState != StaState::CONNECTED)) {
    status_.staIp[0] = '\0';
    status_.staLinkMetricsValid = false;
    status_.staRssi = 0;
    status_.staSignalQualityPct = 0;
  }
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::AP_STARTED, apReady, httpReady);
  lifecycleLogf("%s%s at %s",
                keepStation ? "WiFi recovery SoftAP " : "WiFi SoftAP ",
                apReady && (httpReady || !wantHttp) ? "ready" : "failed",
                AP_IP);
  if (!apReady || (wantHttp && !httpReady)) {
    if (!keepHttp) {
      stopHttpServer();
    }
    WiFi.softAPdisconnect(true);
    if (!keepStation) {
      WiFi.mode(WIFI_OFF);
    }
    portENTER_CRITICAL(&dataMux_);
    status_.apActive = false;
    status_.networkActive = false;
    portEXIT_CRITICAL(&dataMux_);
    return false;
  }
  return true;
}

void ShotStopperNetwork::stopNetwork() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  stopHttpServer();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  portENTER_CRITICAL(&dataMux_);
  scanRequested_ = false;
  scan_ = WifiScanSnapshot{};
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                            : StaState::NOT_CONFIGURED;
  status_.staIp[0] = '\0';
  status_.staLinkMetricsValid = false;
  status_.staRssi = 0;
  status_.staSignalQualityPct = 0;
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
}

void ShotStopperNetwork::serviceStaState(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (!status.wifiConfigured) {
    return;
  }

  if (status.staState == StaState::CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::DISCONNECTED;
      status_.networkActive = false;
      status_.staIp[0] = '\0';
      status_.staLinkMetricsValid = false;
      status_.staRssi = 0;
      status_.staSignalQualityPct = 0;
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
          static_cast<int32_t>(WiFi.status()));
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      // After a prior successful STA join this session, never auto-raise SoftAP
      // on link loss — only retry station (SoftAP via AP_START or reboot).
      staConnectStartedAtMs_ = now;
      staReconnectAttemptAtMs_ = now;
      lifecycleLog("WiFi STA disconnected; retrying STA (no SoftAP after prior connect)");
      if (!staReconnectHeld_ && !wifiScanInProgress()) {
        beginStationConnect(settingsCopy(), now);
      } else if (staReconnectHeld_) {
        lifecycleLog("STA reconnect held; skipping retry");
      }
      return;
    }
    if (status.apActive && !apKeepRequested_) {
      stopSoftApKeepStation();
    }
    {
      const int32_t rssi = WiFi.RSSI();
      portENTER_CRITICAL(&dataMux_);
      status_.staLinkMetricsValid = true;
      status_.staRssi = clampWifiRssi(rssi);
      status_.staSignalQualityPct = wifiRssiToSignalQualityPct(rssi);
      portEXIT_CRITICAL(&dataMux_);
    }
    if (!httpStartHeld_ && server_ == nullptr &&
        static_cast<int32_t>(now - httpRetryAtMs_) >= 0) {
      const bool httpReady = startHttpServer();
      httpRetryAtMs_ = httpReady ? 0 : now + HTTP_RETRY_MS;
      portENTER_CRITICAL(&dataMux_);
      status_.networkActive = httpReady;
      portEXIT_CRITICAL(&dataMux_);
      if (!httpReady) {
        lifecycleLog("HTTP start retry failed on STA");
      }
    }
    if (staConfirmArmed_ &&
        settingsCopy().staConfigState ==
            static_cast<uint8_t>(StaConfigState::PENDING) &&
        static_cast<int32_t>(now - staConfirmDeadlineMs_) >= 0) {
      WiFi.disconnect(false, false);
      if (!revertPendingNetwork(now, "confirm timeout")) {
        startupComplete_ = false;
        if (startupFailures_ < UINT8_MAX) {
          ++startupFailures_;
        }
        networkRetryAtMs_ = now + NETWORK_RETRY_MIN_MS;
        portENTER_CRITICAL(&dataMux_);
        status_.startupFailures = startupFailures_;
        portEXIT_CRITICAL(&dataMux_);
      } else {
        startupFailures_ = 0;
        portENTER_CRITICAL(&dataMux_);
        status_.startupFailures = 0;
        portEXIT_CRITICAL(&dataMux_);
      }
    }
    return;
  }

  if ((status.staState == StaState::CONNECTING ||
       status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      WiFi.status() == WL_CONNECTED) {
    char address[16] = {};
    formatIp(WiFi.localIP(), address);
    if (!apKeepRequested_) {
      stopSoftApKeepStation();
    }
    const bool mayStartHttp = !httpStartHeld_ &&
                              static_cast<int32_t>(now - httpRetryAtMs_) >= 0;
    const bool httpReady = mayStartHttp && startHttpServer();
    httpRetryAtMs_ = httpReady || httpStartHeld_ ? 0 : now + HTTP_RETRY_MS;
    staEverConnected_ = true;
    {
      const int32_t rssi = WiFi.RSSI();
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::CONNECTED;
      status_.networkActive = httpReady;
      strncpy(status_.staIp, address, sizeof(status_.staIp) - 1);
      status_.staLinkMetricsValid = true;
      status_.staRssi = clampWifiRssi(rssi);
      status_.staSignalQualityPct = wifiRssiToSignalQualityPct(rssi);
      status_.windowRemainingMs = 0;
      portEXIT_CRITICAL(&dataMux_);
    }
    log(DebugCategory::NETWORK, DebugCode::STA_CONNECTED);
    lifecycleLogf("WiFi STA connected; IP: %s http=%s", address,
                  httpReady ? "up" : (httpStartHeld_ ? "held" : "down"));
    staNtpEligibleAtMs_ = now + NTP_STA_SETTLE_MS;
    ntpRearmPending_ = true;
    if (settingsCopy().staConfigState ==
        static_cast<uint8_t>(StaConfigState::PENDING)) {
      armPendingConfirmWindow(now);
    }
    return;
  }

  if (status.staState == StaState::CONNECTING &&
      static_cast<uint32_t>(now - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS &&
      !status.apActive) {
    // SoftAP auto-raise is boot/bootstrap only (never after a prior CONNECTED).
    if (wifiScanInProgress()) {
      return;
    }
    WiFi.disconnect(false, false);
    portENTER_CRITICAL(&dataMux_);
    status_.staState = StaState::FAILED;
    portEXIT_CRITICAL(&dataMux_);
    log(DebugCategory::NETWORK, DebugCode::STA_FAILED,
        static_cast<int32_t>(WiFi.status()));
    const bool pending = settingsCopy().staConfigState ==
                         static_cast<uint8_t>(StaConfigState::PENDING);
    if (pending) {
      lifecycleLog("WiFi STA failed; reverting pending config");
      if (!revertPendingNetwork(now, "connect timeout")) {
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
    if (staEverConnected_) {
      lifecycleLog("WiFi STA failed; SoftAP suppressed after prior connect");
      startupFailures_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      status_.staState = StaState::DISCONNECTED;
      portEXIT_CRITICAL(&dataMux_);
      staReconnectAttemptAtMs_ = now;
      return;
    }
    lifecycleLog("WiFi STA failed; SoftAP up, STA retry continues");
    if (apStartHeld_) {
      lifecycleLog("SoftAP raise held after STA fail");
      startupFailures_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      status_.staState = StaState::DISCONNECTED;
      portEXIT_CRITICAL(&dataMux_);
    } else if (!ensureAccessPoint(now)) {
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
      status_.staState = StaState::DISCONNECTED;
      portEXIT_CRITICAL(&dataMux_);
    }
    staReconnectAttemptAtMs_ = now;
    return;
  }

  // SoftAP after STA bootstrap window while still DISCONNECTED/FAILED (e.g.
  // scan delayed the reconnect begin). Never after a prior CONNECTED session.
  if (!staEverConnected_ && !status.apActive &&
      (status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      static_cast<uint32_t>(now - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS &&
      !wifiScanInProgress()) {
    if (apStartHeld_) {
      lifecycleLog("SoftAP raise held");
    } else if (!ensureAccessPoint(now)) {
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
      return;
    }
    staReconnectAttemptAtMs_ = now;
  }

  if (status.staState == StaState::CONNECTING && status.apActive &&
      !wifiScanInProgress()) {
    const wl_status_t wifiStatus = WiFi.status();
    const bool terminalFail = wifiStatus == WL_CONNECT_FAILED ||
                              wifiStatus == WL_NO_SSID_AVAIL ||
                              wifiStatus == WL_CONNECTION_LOST;
    const bool attemptAged =
        static_cast<uint32_t>(now - staConnectStartedAtMs_) >=
        STA_RECOVERY_ATTEMPT_MS;
    if (terminalFail || attemptAged) {
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::DISCONNECTED;
      portEXIT_CRITICAL(&dataMux_);
      staReconnectAttemptAtMs_ = now;
      if (terminalFail) {
        lifecycleLog("WiFi STA recovery attempt ended; will retry");
      }
      return;
    }
  }

  if ((status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      !staReconnectHeld_ && !wifiScanInProgress() &&
      static_cast<uint32_t>(now - staReconnectAttemptAtMs_) >=
          STA_RECONNECT_INTERVAL_MS) {
    beginStationConnect(settingsCopy(), now);
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
    if (canceled) {
      abortWifiScan(now, false);
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
  uint32_t scanStartedAtMs = 0;
  portENTER_CRITICAL(&dataMux_);
  scanStartedAtMs = scan_.updatedAtMs;
  portEXIT_CRITICAL(&dataMux_);
  if (static_cast<uint32_t>(now - scanStartedAtMs) >= WIFI_SCAN_TIMEOUT_MS) {
    abortWifiScan(now, true);
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

  if (isCliNetworkAction(acceptedCommand_.type)) {
    recordCommandResult(acceptedCommand_.requestId,
                        CommandResultState::RESERVED);
    ++acceptedCommandAttempts_;
    if (!processAcceptedCommand(acceptedCommand_)) {
      if (acceptedCommandAttempts_ < COMMAND_MAX_ATTEMPTS) {
        acceptedCommandRetryAtMs_ =
            now + COMMAND_RETRY_MIN_MS * acceptedCommandAttempts_;
        log(DebugCategory::SECURITY, DebugCode::COMMAND_RETRY,
            acceptedCommand_.requestId, acceptedCommandAttempts_);
        return;
      }
      recordCommandResult(acceptedCommand_.requestId,
                          CommandResultState::FAILED);
      actionLogf("ERR %s failed after retries",
                 webCommandTypeName(acceptedCommand_.type));
      acceptedCommandPending_ = false;
      acceptedCommand_ = WebCommand{};
      return;
    }
    recordCommandResult(acceptedCommand_.requestId,
                        CommandResultState::APPLIED);
    noteCliNetworkProgress();
    acceptedCommandPending_ = false;
    acceptedCommand_ = WebCommand{};
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

  // Maintenance commands need a large ControlStatusSnapshot. Keep that
  // object in a separate frame so CLI actions such as WEBUI_RESTART do not
  // carry it into httpd_stop() and LwIP.
  processAcceptedMaintenanceCommand(now);
}

void ShotStopperNetwork::processAcceptedMaintenanceCommand(uint32_t now) {
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
                           command.type == WebCommandType::RESET_AP_PASSWORD ||
                           command.type == WebCommandType::RESET_NETWORK_AP ||
                           command.type == WebCommandType::FACTORY_RESET ||
                           command.type == WebCommandType::RESTART
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
  if (isCliNetworkAction(command.type)) {
    return handleCliNetworkAction(command, millis());
  }
  if (command.type == WebCommandType::START_WIFI_SCAN) {
    portENTER_CRITICAL(&dataMux_);
    scan_ = WifiScanSnapshot{};
    scan_.state = WifiScanState::QUEUED;
    scan_.updatedAtMs = millis();
    scanRequested_ = true;
    scanMaintenanceLeaseId_ = command.maintenanceLeaseId;
    scanRequestId_ = command.requestId;
    portEXIT_CRITICAL(&dataMux_);
    return true;
  }
  return processPersistedCommand(command);
}

bool ShotStopperNetwork::processPersistedCommand(const WebCommand &command) {
  PersistedSettings next = settingsCopy();
  bool persist = false;
  bool factoryReset = false;
  switch (command.type) {
    case WebCommandType::PERSIST_RUNTIME:
      if (command.persistPresets && callbacks_.copyPresetBank != nullptr) {
        callbacks_.copyPresetBank(&next.presets);
      }
      {
        const RuntimeConfig composed =
            composeEffectiveConfig(command.config, next.presets);
        if (validateRuntimeConfig(composed) != ConfigValidationError::NONE) {
          log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
          return false;
        }
      }
      next.runtime = command.config;
      persist = true;
      break;

    case WebCommandType::SAVE_NETWORK: {
      char password[WIFI_PASSWORD_CAPACITY] = {};
      const bool reusePassword = shouldReuseSavedWifiCredentials(
          command.ssid, command.password, command.openNetwork,
          next.staConfigured, next.staSsid, next.staOpen);
      strncpy(password, reusePassword ? next.staPassword : command.password,
              sizeof(password) - 1);
      if (!validWifiSsid(command.ssid) ||
          !validWifiPassword(password, command.openNetwork) ||
          !validStaAddressConfig(command.staIpMode, command.staIp,
                                 command.staNetmask, command.staGateway,
                                 command.staDns1, command.staDns2)) {
        memset(password, 0, sizeof(password));
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      if (next.staConfigured &&
          next.staConfigState ==
              static_cast<uint8_t>(StaConfigState::CONFIRMED)) {
        copyActiveStaToLkg(next);
      }
      next.staConfigured = true;
      next.staOpen = command.openNetwork;
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      strncpy(next.staSsid, command.ssid, sizeof(next.staSsid) - 1);
      strncpy(next.staPassword, password, sizeof(next.staPassword) - 1);
      memset(password, 0, sizeof(password));
      next.staIpMode = command.staIpMode;
      memcpy(next.staIp, command.staIp, sizeof(next.staIp));
      memcpy(next.staNetmask, command.staNetmask, sizeof(next.staNetmask));
      memcpy(next.staGateway, command.staGateway, sizeof(next.staGateway));
      memcpy(next.staDns1, command.staDns1, sizeof(next.staDns1));
      memcpy(next.staDns2, command.staDns2, sizeof(next.staDns2));
      next.staConfigState = static_cast<uint8_t>(StaConfigState::PENDING);
      persist = true;
      restartPending_ = true;
      break;
    }

    case WebCommandType::FORGET_NETWORK:
      clearStaNetwork(next);
      persist = true;
      restartPending_ = true;
      break;

    case WebCommandType::CHANGE_AP_PASSWORD:
      if (!setAccessPointPassword(next, command.password)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      break;

    case WebCommandType::RESET_AP_PASSWORD:
      if (!initializeDefaultAccessPointPassword(next)) {
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      log(DebugCategory::SECURITY, DebugCode::AP_PASSWORD_RESET);
      break;

    case WebCommandType::RESET_NETWORK_AP:
      clearStaNetwork(next);
      if (!initializeDefaultAccessPointPassword(next)) {
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
      if (callbacks_.clearLastShot != nullptr) {
        callbacks_.clearLastShot();
      }
      factoryReset = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::FACTORY_RESET);
      break;

    case WebCommandType::RESTART:
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::RESTART_REQUESTED);
      break;

    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
    case WebCommandType::START_WIFI_SCAN:
      return false;

    default:
      return false;
  }

  if (persist) {
    overlayLiveShotSettings(next);
    if (!savePersistedSettings(next)) {
      restartPending_ = false;
      apRestartPending_ = false;
      // NVS write failure is not a config validation reject. PERSIST_RUNTIME
      // failures are reported clearly when the maintenance lease completes.
      if (command.type != WebCommandType::PERSIST_RUNTIME) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
      }
      return false;
    }
    portENTER_CRITICAL(&dataMux_);
    settings_ = next;
    status_.wifiConfigured = next.staConfigured;
    status_.staConfigState = next.staConfigState;
    portEXIT_CRITICAL(&dataMux_);
    publishConfiguredAddressStatus();
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  } else if (factoryReset) {
    portENTER_CRITICAL(&dataMux_);
    settings_ = next;
    status_.wifiConfigured = false;
    status_.staConfigState =
        static_cast<uint8_t>(StaConfigState::CONFIRMED);
    portEXIT_CRITICAL(&dataMux_);
    publishConfiguredAddressStatus();
    log(DebugCategory::CONFIG, DebugCode::CONFIG_PERSISTED,
        static_cast<int32_t>(next.runtime.revision));
  }

  if (restartPending_ || apRestartPending_) {
    restartRequestedAtMs_ = millis();
  }
  return true;
}

bool ShotStopperNetwork::handleCliNetworkAction(const WebCommand &command,
                                                uint32_t now) {
  refreshExtendedStatus(now);
  switch (command.type) {
    case WebCommandType::WIFI_CONNECT:
    case WebCommandType::WIFI_DISCONNECT:
    case WebCommandType::WIFI_RESTART:
      return handleCliWifiAction(command, now);
    case WebCommandType::AP_START:
    case WebCommandType::AP_STOP:
      return handleCliApAction(command, now);
    case WebCommandType::WEBUI_START:
    case WebCommandType::WEBUI_STOP:
    case WebCommandType::WEBUI_RESTART:
      return handleCliWebUiAction(command, now);
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliWifiAction(const WebCommand &command,
                                             uint32_t now) {
  switch (command.type) {
    case WebCommandType::WIFI_CONNECT: {
      const PersistedSettings settings = settingsCopy();
      if (!settings.staConfigured) {
        actionLog("ERR no STA configured; use SET_WIFI");
        printActionSnapshot("WIFI_CONNECT", false);
        return true;
      }
      const NetworkStatusSnapshot status = snapshot();
      if (status.staState == StaState::CONNECTED &&
          WiFi.status() == WL_CONNECTED) {
        actionLog("OK already connected");
        printActionSnapshot("WIFI_CONNECT", true);
        return true;
      }
      staReconnectHeld_ = false;
      beginStationConnect(settings, now);
      actionLog("WIFI_CONNECT associating saved STA");
      printActionSnapshot("WIFI_CONNECT", true);
      return true;
    }
    case WebCommandType::WIFI_DISCONNECT: {
      staReconnectHeld_ = true;
      WiFi.disconnect(false, false);
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      portENTER_CRITICAL(&dataMux_);
      status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                                : StaState::NOT_CONFIGURED;
      status_.staIp[0] = '\0';
      status_.staLinkMetricsValid = false;
      status_.staRssi = 0;
      status_.staSignalQualityPct = 0;
      portEXIT_CRITICAL(&dataMux_);
      actionLog("WIFI_DISCONNECT STA down; reconnect held");
      printActionSnapshot("WIFI_DISCONNECT", true);
      return true;
    }
    case WebCommandType::WIFI_RESTART: {
      const PersistedSettings settings = settingsCopy();
      if (!settings.staConfigured) {
        actionLog("ERR no STA configured; use SET_WIFI");
        printActionSnapshot("WIFI_RESTART", false);
        return true;
      }
      staReconnectHeld_ = false;
      stopNtp();
      staNtpEligibleAtMs_ = 0;
      g_wallClock.markDisabled();
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::DISCONNECTED;
      status_.staIp[0] = '\0';
      status_.staLinkMetricsValid = false;
      portEXIT_CRITICAL(&dataMux_);
      beginStationConnect(settings, now);
      actionLog("WIFI_RESTART reconnecting saved STA");
      printActionSnapshot("WIFI_RESTART", true);
      return true;
    }
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliApAction(const WebCommand &command,
                                           uint32_t now) {
  switch (command.type) {
    case WebCommandType::AP_START: {
      apStartHeld_ = false;
      apKeepRequested_ = true;
      const bool ok = ensureAccessPoint(now, true);
      if (!ok) {
        apKeepRequested_ = false;
      }
      actionLog(ok ? "AP_START SoftAP requested" : "ERR AP_START failed");
      printActionSnapshot("AP_START", ok);
      return true;
    }
    case WebCommandType::AP_STOP: {
      apStartHeld_ = true;
      apKeepRequested_ = false;
      const bool staUp = snapshot().staState == StaState::CONNECTED &&
                         WiFi.status() == WL_CONNECTED;
      if (staUp) {
        stopSoftApLeaveHttp();
      } else {
        stopSoftAp(true);
      }
      actionLog("AP_STOP SoftAP down; auto-raise held");
      printActionSnapshot("AP_STOP", true);
      return true;
    }
    default:
      return false;
  }
}

bool ShotStopperNetwork::handleCliWebUiAction(const WebCommand &command,
                                              uint32_t now) {
  (void)now;
  switch (command.type) {
    case WebCommandType::WEBUI_START: {
      httpStartHeld_ = false;
      if (server_ != nullptr) {
        actionLog("OK already running");
        printActionSnapshot("WEBUI_START", true);
        return true;
      }
      const bool ok = startHttpServer();
      portENTER_CRITICAL(&dataMux_);
      status_.networkActive = ok && (status_.apActive ||
                                     status_.staState == StaState::CONNECTED);
      status_.httpActive = ok;
      portEXIT_CRITICAL(&dataMux_);
      actionLog(ok ? "WEBUI_START httpd up" : "ERR WEBUI_START httpd_start failed");
      if (!ok) {
        actionLogf("WEBUI_START heap hint free=%u",
                   static_cast<unsigned>(ESP.getFreeHeap()));
      }
      printActionSnapshot("WEBUI_START", ok);
      return true;
    }
    case WebCommandType::WEBUI_STOP: {
      httpStartHeld_ = true;
      stopHttpServer();
      portENTER_CRITICAL(&dataMux_);
      status_.networkActive = false;
      status_.httpActive = false;
      portEXIT_CRITICAL(&dataMux_);
      actionLog("WEBUI_STOP httpd down; auto-start held");
      printActionSnapshot("WEBUI_STOP", true);
      return true;
    }
    case WebCommandType::WEBUI_RESTART: {
      httpStartHeld_ = false;
      stopHttpServer();
      const bool ok = startHttpServer();
      portENTER_CRITICAL(&dataMux_);
      status_.networkActive = ok && (status_.apActive ||
                                     status_.staState == StaState::CONNECTED);
      status_.httpActive = ok;
      portEXIT_CRITICAL(&dataMux_);
      actionLog(ok ? "WEBUI_RESTART httpd up" : "ERR WEBUI_RESTART failed");
      if (!ok) {
        actionLogf("WEBUI_RESTART heap hint free=%u",
                   static_cast<unsigned>(ESP.getFreeHeap()));
      }
      printActionSnapshot("WEBUI_RESTART", ok);
      return true;
    }
    default:
      return false;
  }
}

void ShotStopperNetwork::log(DebugCategory category, DebugCode code,
                             int32_t argument1, int32_t argument2) {
  if (callbacks_.addDebugEvent != nullptr) {
    callbacks_.addDebugEvent(category, code, argument1, argument2);
  }
}

void ShotStopperNetwork::actionLog(const char *message) {
  if (message != nullptr) {
    Serial.println(message);
  }
}

void ShotStopperNetwork::actionLogf(const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[192] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  Serial.println(line);
}

void ShotStopperNetwork::lifecycleLog(const char *message) {
  if (serialDebugEnabled() && message != nullptr) {
    Serial.println(message);
  }
}

void ShotStopperNetwork::lifecycleLogf(const char *fmt, ...) {
  if (!serialDebugEnabled() || fmt == nullptr) {
    return;
  }
  char line[192] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  Serial.println(line);
}

void ShotStopperNetwork::printActionSnapshot(const char *command, bool ok) {
  refreshExtendedStatus(millis());
  const NetworkStatusSnapshot status = snapshot();
  actionLogf("%s %s ssid=%s mode=%s wifiStatus=%ld %s sta=%s ip=%s rssi=%d "
             "ap=%s apIp=%s clients=%u http=%s holds sta/ap/http=%d/%d/%d",
             command != nullptr ? command : "NETWORK",
             ok ? "OK" : "ERR",
             status.staSsid[0] != '\0' ? status.staSsid : "-",
             serialCliWifiModeName(status.wifiMode),
             static_cast<long>(status.wifiStatus),
             serialCliWlStatusName(status.wifiStatus),
             staStateName(status.staState),
             status.staIp[0] != '\0' ? status.staIp : "-",
             status.staLinkMetricsValid ? static_cast<int>(status.staRssi) : 0,
             status.apActive ? "up" : "down",
             status.apIp,
             static_cast<unsigned>(status.apClients),
             status.httpActive ? "up" : "down",
             status.staReconnectHeld ? 1 : 0,
             status.apStartHeld ? 1 : 0,
             status.httpStartHeld ? 1 : 0);
}

void ShotStopperNetwork::noteCliNetworkProgress() {
  if (startupComplete_) {
    return;
  }
  const NetworkStatusSnapshot status = snapshot();
  if (!status.apActive && server_ == nullptr &&
      status.staState != StaState::CONNECTING &&
      status.staState != StaState::CONNECTED) {
    return;
  }
  startupComplete_ = true;
  startupFailures_ = 0;
  networkRetryAtMs_ = 0;
  portENTER_CRITICAL(&dataMux_);
  status_.startupFailures = 0;
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::refreshExtendedStatus(uint32_t now) {
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(now);
  const wl_status_t wifiStatus = WiFi.status();
  const bool staConnected = wifiStatus == WL_CONNECTED;
  const uint8_t wifiMode = static_cast<uint8_t>(WiFi.getMode());
  const uint8_t channel = static_cast<uint8_t>(WiFi.channel());
  const String staMac = WiFi.macAddress();
  const String apMac = WiFi.softAPmacAddress();
  const bool ntpArm = ntpMayArm(now, staConnected);
  const bool httpActive = server_ != nullptr;
  const bool factoryPassword = passwordIsFactoryDefault(settings_);
  portENTER_CRITICAL(&dataMux_);
  status_.httpActive = httpActive;
  status_.wifiMode = wifiMode;
  status_.channel = channel;
  status_.wifiStatus = static_cast<int32_t>(wifiStatus);
  status_.staReconnectHeld = staReconnectHeld_;
  status_.apStartHeld = apStartHeld_;
  status_.httpStartHeld = httpStartHeld_;
  status_.apPasswordFactory = factoryPassword;
  status_.scanState = static_cast<uint8_t>(scan_.state);
  status_.ntpState = static_cast<uint8_t>(timeStatus.state);
  status_.ntpMayArm = ntpArm;
  status_.staConnectAgeMs =
      staConnectStartedAtMs_ != 0
          ? static_cast<uint32_t>(now - staConnectStartedAtMs_)
          : 0;
  status_.staReconnectAgeMs =
      staReconnectAttemptAtMs_ != 0
          ? static_cast<uint32_t>(now - staReconnectAttemptAtMs_)
          : 0;
  memset(status_.staMac, 0, sizeof(status_.staMac));
  memset(status_.apMac, 0, sizeof(status_.apMac));
  strncpy(status_.staMac, staMac.c_str(), sizeof(status_.staMac) - 1);
  strncpy(status_.apMac, apMac.c_str(), sizeof(status_.apMac) - 1);
  memset(status_.ntpActiveServer, 0, sizeof(status_.ntpActiveServer));
  strncpy(status_.ntpActiveServer, timeStatus.activeServer,
          sizeof(status_.ntpActiveServer) - 1);
  portEXIT_CRITICAL(&dataMux_);
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
  config.stack_size = HTTP_SERVER_TASK_STACK_SIZE;
  // Web UI serializes API traffic (DEVICE_MAX_INFLIGHT, default 1). Reserve
  // headroom for the HTML/JS/CSS boot burst plus LRU/TCP close. ESP-IDF uses
  // (max_open_sockets + 3) LWIP sockets total.
  // Two browser windows can briefly request HTML, CSS, and JS in parallel.
  // Keep enough client slots for that boot burst while responses still close
  // promptly to limit the ESP's steady-state socket use.
  config.max_open_sockets = 6;
  // Keep one slot of headroom above the owned API routes; /api/v1/ui/unlock
  // is registered separately because it establishes the explicit override.
  config.max_uri_handlers = 36;
  // Safari sends a long UA + Accept-Language + optional Cookie/Sec-Fetch-*;
  // the IDF default (1024) is enough most of the time but intermittent
  // long browser headers have returned 431 Request Header Fields Too Large.
  config.max_req_hdr_len = 2048;
  config.max_resp_headers = 12;
  config.backlog_conn = 3;
  config.lru_purge_enable = true;
  // Keep stuck clients from occupying sockets under BLE/Wi-Fi coex stalls.
  config.recv_wait_timeout = 5;
  config.send_wait_timeout = 5;
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    lifecycleLog("httpd_start failed");
    return false;
  }

  const bool registered =
      registerHandler(server_, "/", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/diagnostic", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/log", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/history", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/admin", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/settings", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/app.js", HTTP_GET, jsHandler) &&
      registerHandler(server_, "/app.css", HTTP_GET, cssHandler) &&
      registerHandler(server_, "/api/v1/ui/claim", HTTP_POST, claimHandler) &&
      registerHandler(server_, "/api/v1/ui/unlock", HTTP_POST, unlockHandler) &&
      registerHandler(server_, "/api/v1/status/home", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/settings", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/admin", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/diagnostic", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/log", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/delete", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/last-shot/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/time/sync", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/config", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/scale/preferred/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/scale/preferred/select", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/presets", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/calibration/reset", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/calibration/reset-guard-samples", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/paddle", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/rinse", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/stop", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/restart", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/factory-reset", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/access-point/password", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/ble-compat", HTTP_PUT, ownedApiHandler);
  if (!registered ||
      httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND,
                                 notFoundHandler) != ESP_OK) {
    stopHttpServer();
    return false;
  }
  return true;
}

void ShotStopperNetwork::stopHttpServer() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
}


esp_err_t ShotStopperNetwork::sendJson(httpd_req_t *request,
                                       const char *status,
                                       const char *json) {
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  // Close after each API response so keep-alive does not pin sockets on the
  // ESP while BLE/Wi-Fi coex delays responses.
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
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

namespace {

constexpr const char *WEB_UI_CLIENT_HEADER = "X-WebUI-Client";

const char *configLockReason(const ControlStatusSnapshot &status) {
  if (status.activeCycle) return "active_shot";
  if (status.relayClosed) return "cn9_closed";
  if (status.physicalPaddleOn) return "paddle_on";
  if (status.maintenanceLeaseActive) return "maintenance";
  if (status.state != StopperState::READY) return "not_ready";
  return "none";
}

bool apiUriMatches(const char *uri, const char *path) {
  if (uri == nullptr || path == nullptr) {
    return false;
  }
  const size_t length = strlen(path);
  return strncmp(uri, path, length) == 0 &&
         (uri[length] == '\0' || uri[length] == '?');
}

bool readWebUiClientId(httpd_req_t *request, char *output,
                       size_t capacity) {
  if (request == nullptr || output == nullptr || capacity < 2) {
    return false;
  }
  const size_t length =
      httpd_req_get_hdr_value_len(request, WEB_UI_CLIENT_HEADER);
  if (length < 16 || length + 1 > capacity) {
    return false;
  }
  if (httpd_req_get_hdr_value_str(request, WEB_UI_CLIENT_HEADER, output,
                                   capacity) != ESP_OK) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const char character = output[index];
    if (!((character >= '0' && character <= '9') ||
          (character >= 'a' && character <= 'f'))) {
      output[0] = '\0';
      return false;
    }
  }
  return true;
}

}  // namespace

esp_err_t ShotStopperNetwork::claimHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    return sendError(request, STATUS_BAD_REQUEST, "UI_CLIENT_INVALID",
                     "X-WebUI-Client must be a 16-24 character lowercase hex id.");
  }
  portENTER_CRITICAL(&self.dataMux_);
  memcpy(self.activeWebUiClientId_, clientId, sizeof(clientId));
  self.webUiOverrideActive_ = false;
  portEXIT_CRITICAL(&self.dataMux_);
  memset(clientId, 0, sizeof(clientId));
  return sendJson(request, STATUS_OK, "{\"active\":true}");
}

esp_err_t ShotStopperNetwork::unlockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireActiveWebUiClient(request)) return ESP_OK;
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "An explicit confirmation is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char confirmation[32] = {};
  static const char *const fields[] = {"confirm"};
  const bool confirmed = root != nullptr &&
                         jsonHasOnlyUniqueFields(root, fields, 1) &&
                         jsonString(root, "confirm", confirmation,
                                    sizeof(confirmation), false) &&
                         strcmp(confirmation, "UNSAFE_WEBUI_OVERRIDE") == 0;
  if (root != nullptr) cJSON_Delete(root);
  memset(body, 0, sizeof(body));
  memset(confirmation, 0, sizeof(confirmation));
  if (!confirmed) {
    return sendError(request, STATUS_UNPROCESSABLE, "UNLOCK_NOT_CONFIRMED",
                     "The unsafe WebUI override was not explicitly confirmed.");
  }
  portENTER_CRITICAL(&self.dataMux_);
  self.webUiOverrideActive_ = true;
  portEXIT_CRITICAL(&self.dataMux_);
  return sendJson(request, STATUS_OK, "{\"unlocked\":true}");
}

bool ShotStopperNetwork::webUiOverrideAllowed(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) return false;
  bool allowed = false;
  portENTER_CRITICAL(&dataMux_);
  allowed = webUiOverrideActive_ && activeWebUiClientId_[0] != '\0' &&
            strcmp(activeWebUiClientId_, clientId) == 0;
  portEXIT_CRITICAL(&dataMux_);
  memset(clientId, 0, sizeof(clientId));
  return allowed;
}

bool ShotStopperNetwork::webUiConfigurationAllowed(
    httpd_req_t *request, const ControlStatusSnapshot &status) {
  if (controlAllowsConfiguration(status)) {
    clearWebUiOverrideIfSafe(status);
    return true;
  }
  return webUiOverrideAllowed(request);
}

void ShotStopperNetwork::clearWebUiOverrideIfSafe(
    const ControlStatusSnapshot &status) {
  if (!controlAllowsConfiguration(status)) return;
  portENTER_CRITICAL(&dataMux_);
  webUiOverrideActive_ = false;
  portEXIT_CRITICAL(&dataMux_);
}

bool ShotStopperNetwork::requireActiveWebUiClient(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    sendError(request, STATUS_CONFLICT, "UI_CLAIM_REQUIRED",
              "Claim WebUI control before using this API.");
    return false;
  }
  bool active = false;
  portENTER_CRITICAL(&dataMux_);
  active = activeWebUiClientId_[0] != '\0' &&
           strcmp(activeWebUiClientId_, clientId) == 0;
  portEXIT_CRITICAL(&dataMux_);
  memset(clientId, 0, sizeof(clientId));
  if (!active) {
    sendError(request, STATUS_CONFLICT, "UI_TAKEN_OVER",
              "This WebUI window is inactive. Reactivate to continue.");
    return false;
  }
  return true;
}

esp_err_t ShotStopperNetwork::ownedApiHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireActiveWebUiClient(request)) {
    return ESP_OK;
  }
  if (apiUriMatches(request->uri, "/api/v1/status/home") ||
      apiUriMatches(request->uri, "/api/v1/status/settings") ||
      apiUriMatches(request->uri, "/api/v1/status/admin") ||
      apiUriMatches(request->uri, "/api/v1/status/diagnostic")) {
    return statusHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/log")) return logHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots")) return shotsHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/clear")) return shotsClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/delete")) return shotsDeleteHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/last-shot/clear")) return lastShotClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/time/sync")) return timeSyncHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/config")) return configHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/scale/preferred/clear")) return preferredScaleClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/scale/preferred/select")) return preferredScaleSelectHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/presets")) return presetsHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/calibration/reset")) return resetCalibrationHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/calibration/reset-guard-samples")) return resetGuardSamplesHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/paddle")) return paddleHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/rinse")) return rinseHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/stop")) return stopHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/restart")) return restartHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/factory-reset")) return factoryResetHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/network/scan")) {
    return request->method == HTTP_GET ? wifiScanStatusHandler(request)
                                       : wifiScanStartHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/network")) return networkHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/access-point/password")) return apPasswordHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/ble-compat")) return bleCompatHandler(request);
  return sendError(request, STATUS_NOT_FOUND, "NOT_FOUND", "Unknown API route.");
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

static void formatWebUiEtag(char etag[WEB_UI_ETAG_CAPACITY]) {
  // Asset tag changes when embedded HTML/JS/CSS/logo change, even if the git
  // version string is unchanged (dirty rebuilds). Required so immutable
  // /app.js?v=… cache busts after reflashes.
  snprintf(etag, WEB_UI_ETAG_CAPACITY, "\"%s.%s\"", FW_VERSION,
           WEB_UI_ASSET_TAG);
}

static bool ifNoneMatchEquals(httpd_req_t *request, const char *etag) {
  const size_t length = httpd_req_get_hdr_value_len(request, "If-None-Match");
  if (length == 0 || length >= IF_NONE_MATCH_CAPACITY) {
    return false;
  }
  char value[IF_NONE_MATCH_CAPACITY] = {};
  if (httpd_req_get_hdr_value_str(request, "If-None-Match", value,
                                  sizeof(value)) != ESP_OK) {
    return false;
  }
  return strcmp(value, etag) == 0;
}

esp_err_t ShotStopperNetwork::rootHandler(httpd_req_t *request) {
  char etag[WEB_UI_ETAG_CAPACITY] = {};
  formatWebUiEtag(etag);
  if (ifNoneMatchEquals(request, etag)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "ETag", etag);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(request, "ETag", etag);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(
      request, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; "
      "connect-src 'self'; frame-ancestors 'none'");
  httpd_resp_set_hdr(request, "Connection", "close");
  return httpd_resp_send(
      request, reinterpret_cast<const char *>(SHOT_STOPPER_WEB_UI_GZIP),
      SHOT_STOPPER_WEB_UI_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::jsHandler(httpd_req_t *request) {
  char etag[WEB_UI_ETAG_CAPACITY] = {};
  formatWebUiEtag(etag);
  if (ifNoneMatchEquals(request, etag)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control",
                       "public, max-age=31536000, immutable");
    httpd_resp_set_hdr(request, "ETag", etag);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, "application/javascript; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "ETag", etag);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Connection", "close");
  return httpd_resp_send(
      request, reinterpret_cast<const char *>(SHOT_STOPPER_WEB_JS_GZIP),
      SHOT_STOPPER_WEB_JS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::cssHandler(httpd_req_t *request) {
  char etag[WEB_UI_ETAG_CAPACITY] = {};
  formatWebUiEtag(etag);
  if (ifNoneMatchEquals(request, etag)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control",
                       "public, max-age=31536000, immutable");
    httpd_resp_set_hdr(request, "ETag", etag);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, "text/css; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "ETag", etag);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Connection", "close");
  return httpd_resp_send(
      request, reinterpret_cast<const char *>(SHOT_STOPPER_WEB_CSS_GZIP),
      SHOT_STOPPER_WEB_CSS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::notFoundHandler(httpd_req_t *request,
                                              httpd_err_code_t) {
  // Keep API 404s as JSON; browser paths bounce to Home.
  if (strncmp(request->uri, "/api/", 5) == 0) {
    return sendError(request, STATUS_NOT_FOUND, "NOT_FOUND",
                     "The requested resource was not found.");
  }
  httpd_resp_set_status(request, "302 Found");
  httpd_resp_set_hdr(request, "Location", "/");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  return httpd_resp_send(request, nullptr, 0);
}


const char *ShotStopperNetwork::stateLabel(StopperState state) {
  switch (state) {
    case StopperState::REQUIRES_OFF: return "Paddle release required";
    case StopperState::READY: return "Ready";
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

const char *activeCycleShotTypeLabel(const ControlStatusSnapshot &control) {
  if (!control.activeCycle) {
    return "idle";
  }
  if (control.state == StopperState::RINSE) {
    return "rinse";
  }
  return shotLogTypeName(shotLogTypeFromCycle(
      control.state, control.cycleStartedWithScale, control.cycleTimerOnly,
      control.cycleAutomaticBrew));
}

const char *ShotStopperNetwork::endReasonName(EndReason reason) {
  switch (reason) {
    case EndReason::NONE: return "NONE";
    case EndReason::PADDLE: return "PADDLE";
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
    case EndReason::FAST_EXTRACTION_MAX_WEIGHT:
      return "FAST_EXTRACTION_MAX_WEIGHT";
    case EndReason::FAST_EXTRACTION_MIN_TIME:
      return "FAST_EXTRACTION_MIN_TIME";
    case EndReason::SLOW_EXTRACTION_MAX_TIME:
      return "SLOW_EXTRACTION_MAX_TIME";
    case EndReason::SLOW_EXTRACTION_MIN_WEIGHT:
      return "SLOW_EXTRACTION_MIN_WEIGHT";
    case EndReason::AUTO_TO_MANUAL_GUARD:
      return "AUTO_TO_MANUAL_GUARD";
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
  // The WebUI and every API route are deliberately public.
  self.requestPendingNetworkConfirm();

  const StatusPage page = parseStatusPage(request->uri);
  if (page == StatusPage::Unknown) {
    return sendError(request, "404 Not Found", "STATUS_PAGE_UNKNOWN",
                     "Unknown status page; use /api/v1/status/{home|settings|admin|diagnostic}.");
  }

  // JSON / snapshot work lives in one PSRAM (or internal fallback) blob.
  if (!self.lockWorkBuf()) {
    return self.workBufBusy(request);
  }

  ControlStatusSnapshot &control = self.workBuf_->control;
  self.callbacks_.copyControlStatus(control);
  self.clearWebUiOverrideIfSafe(control);
  const bool configMutable = controlAllowsConfiguration(control);
  const bool webUiOverrideActive = self.webUiOverrideAllowed(request);
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());

  char currentWeight[32] = "null";
  char observedWeight[32] = "null";
  char lastShotWeight[32] = "null";
  char scaleTimer[32] = "null";
  char staRssiJson[16] = "null";
  char staSignalQualityJson[16] = "null";
  char staChannelJson[8] = "null";
  if (control.currentWeightValid) {
    snprintf(currentWeight, sizeof(currentWeight), "%.2f",
             static_cast<double>(control.currentWeightG));
  }
  if (control.observedWeightValid) {
    snprintf(observedWeight, sizeof(observedWeight), "%.2f",
             static_cast<double>(control.observedWeightG));
  }
  if (control.currentTimerValid) {
    snprintf(scaleTimer, sizeof(scaleTimer), "%lu",
             static_cast<unsigned long>(control.currentTimerMs));
  }
  if (control.lastShot.valid && control.lastShot.weightValid) {
    snprintf(lastShotWeight, sizeof(lastShotWeight), "%.2f",
             static_cast<double>(control.lastShot.currentWeightG));
  }
  if (network.staLinkMetricsValid) {
    snprintf(staRssiJson, sizeof(staRssiJson), "%d",
             static_cast<int>(network.staRssi));
    snprintf(staSignalQualityJson, sizeof(staSignalQualityJson), "%u",
             static_cast<unsigned>(network.staSignalQualityPct));
  }
  if (network.staState == StaState::CONNECTED && network.channel != 0) {
    snprintf(staChannelJson, sizeof(staChannelJson), "%u",
             static_cast<unsigned>(network.channel));
  }

  char safeNtpCustom[NTP_SERVER_HOST_CAPACITY] = {};
  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  char safeScaleProtocol[24] = {};
  char safeLastShotProtocol[24] = {};
  char safePreferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
  char safePreferredScaleName[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  char safeFirmwareVersion[32] = {};
  char safeStaSsid[WIFI_SSID_CAPACITY] = {};
  sanitizeJsonEmbed(control.config.ntpServerCustom, safeNtpCustom,
                    sizeof(safeNtpCustom));
  sanitizeJsonEmbed(timeStatus.activeServer, safeActiveServer,
                    sizeof(safeActiveServer));
  sanitizeJsonEmbed(control.scaleProtocol, safeScaleProtocol,
                    sizeof(safeScaleProtocol));
  sanitizeJsonEmbed(control.lastShot.scaleProtocol, safeLastShotProtocol,
                    sizeof(safeLastShotProtocol));
  sanitizeJsonEmbed(control.preferredScaleMac, safePreferredScaleMac,
                    sizeof(safePreferredScaleMac));
  sanitizeJsonEmbed(control.preferredScaleName, safePreferredScaleName,
                    sizeof(safePreferredScaleName));
  sanitizeJsonEmbed(FW_VERSION, safeFirmwareVersion,
                    sizeof(safeFirmwareVersion));
  sanitizeJsonEmbed(network.staSsid, safeStaSsid, sizeof(safeStaSsid));

  const bool needPresets =
      page == StatusPage::Home || page == StatusPage::Settings;
  const bool needHistory = page == StatusPage::Settings;
  if (needPresets) {
    buildSlimPresetsJson(control);
  } else {
    g_work->presetsJson[0] = '{';
    g_work->presetsJson[1] = '}';
    g_work->presetsJson[2] = 0;
  }
  if (needHistory) {
    buildScaleHistoryJson(control);
  } else {
    g_work->historyJson[0] = '[';
    g_work->historyJson[1] = ']';
    g_work->historyJson[2] = 0;
  }

  const uint32_t cycleFirstDropElapsedMs =
      control.cycleFirstDropMs != 0 && control.cycleStartedAtMs != 0 &&
              static_cast<int32_t>(control.cycleFirstDropMs -
                                   control.cycleStartedAtMs) >= 0
          ? control.cycleFirstDropMs - control.cycleStartedAtMs
          : 0U;

  size_t used = 0;
  const bool liveShot =
      control.activeCycle || control.relayClosed;
  // Shared envelope only: page-specific config (NTP, serial debug, buzzer)
  // is appended below so home/settings polls stay lean.
  bool ok = statusJsonAppend(
      &used,
      "{\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"configMutable\":%s,\"webUiOverrideActive\":%s,\"configLockReason\":\"%s\",\"liveShot\":%s",
      safeFirmwareVersion,
      static_cast<unsigned long>(control.bootId),
      configMutable ? "true" : "false",
      webUiOverrideActive ? "true" : "false", configLockReason(control),
      liveShot ? "true" : "false");
  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(&used, ",\"buzzerSupported\":%s",
                          BUZZER_SUPPORT_ENABLED ? "true" : "false");
  }
  if (ok) {
    ok = statusJsonAppend(
        &used, ",\"config\":{\"revision\":%lu,\"ringRetainLogLevel\":\"%s\"",
        static_cast<unsigned long>(control.config.revision),
        logLevelName(static_cast<LogLevel>(control.config.ringRetainLogLevel)));
  }
  if (ok && page == StatusPage::Admin) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"ntpServerPreset\":\"%s\","
        "\"ntpServerCustom\":\"%s\"",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        ntpPresetId(control.config.ntpServerPreset), safeNtpCustom);
  }
  if (ok && page == StatusPage::Diagnostic) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"serialDebugOutput\":%s",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        control.config.serialDebugOutput ? "true" : "false");
  }

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"soundAlertsEnabled\":%s,\"brewByWeight\":%s,\"goalWeightG\":%u,"
        "\"operationalWallMs\":%lu,\"minBrewTimeMs\":%lu,\"maxBrewTimeMs\":%lu,"
        "\"minRecoveryWeightG\":%.1f,\"maxRecoveryWeightG\":%.1f,"
        "\"fastExtractionGuardEnabled\":%s,\"slowExtractionGuardEnabled\":%s,"
        "\"autoToManualGuardEnabled\":%s,\"avoidBbwShotWithoutScale\":%s,"
        "\"scaleMacCacheMode\":\"%s\"",
        control.config.soundAlertsMuted ? "false" : "true",
        control.config.timerOnly ? "false" : "true",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<unsigned long>(control.config.operationalWallMs),
        static_cast<unsigned long>(control.config.minBrewTimeMs),
        static_cast<unsigned long>(control.config.maxBrewTimeMs),
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<double>(control.config.maxRecoveryWeightG),
        control.config.fastExtractionGuardEnabled ? "true" : "false",
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        control.config.autoToManualGuardEnabled ? "true" : "false",
        control.config.avoidBbwShotWithoutScale ? "true" : "false",
        scaleMacCacheModeId(control.config.scaleMacCacheMode));
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"goalWeightG\":%u,\"weightOffsetG\":%.2f,"
        "\"weightOffsetBaselineG\":%.2f,\"autoTare\":%s,\"brewByWeight\":%s,"
        "\"canTareStartTimer\":%s,\"scaleTimerStopExtraDelayMs\":%lu,"
        "\"dripDelayMs\":%lu,"
        "\"soundAlertsEnabled\":%s,\"firstDropBeep\":%s,"
        "\"paddleReturnReminderBeep\":%s,"
        "\"paddleReturnReminderIntervalMs\":%lu,"
        "\"paddleReturnReminderMaxDurationMs\":%lu,\"paddleMode\":\"%s\","
        "\"buzzerScaleLostBeep\":%s,\"buzzerAutoToManualGuardEndBeep\":%s,"
        "\"buzzerManualNoScaleBeep\":%s,\"buzzerScaleConnectedBeep\":%s,"
        "\"buzzerExtendedPulseRate\":\"%s\","
        "\"buzzerSlowExtendedPulseRate\":\"%s\","
        "\"alertOutputChannel\":\"%s\",\"rinseGestureMs\":%lu,"
        "\"rinseDurationMs\":%lu,\"autoRetare\":%s,\"retareWindowMs\":%lu,"
        "\"minimumCupWeightG\":%.1f,\"retareStabilitySamples\":%u,"
        "\"retareStabilityToleranceG\":%.1f,\"retareStabilityMaxGapMs\":%lu,"
        "\"retareStabilityMinDurationMs\":%lu,\"bbwProtectionMs\":%lu,"
        "\"operationalWallMs\":%lu,\"fastExtractionGuardEnabled\":%s,"
        "\"maxRecoveryWeightG\":%.1f,\"minBrewTimeMs\":%lu,"
        "\"slowExtractionGuardEnabled\":%s,\"minRecoveryWeightG\":%.1f,"
        "\"maxBrewTimeMs\":%lu,\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardLimitMode\":\"%s\","
        "\"autoToManualGuardManualLimitMs\":%lu,"
        "\"autoToManualGuardBaselineMs\":%lu,"
        "\"autoToManualGuardTrendMs\":%lu,\"scaleMacCacheMode\":\"%s\","
        "\"bookooMuteOnBuzzerOnly\":%s,\"bookooConnectBeepLevel\":%u,"
        "\"avoidBbwShotWithoutScale\":%s,\"lastShotCooldownMs\":%lu",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<double>(control.config.weightOffsetG),
        static_cast<double>(control.config.weightOffsetBaselineG),
        control.config.autoTare ? "true" : "false",
        control.config.timerOnly ? "false" : "true",
        control.config.canTareStartTimer ? "true" : "false",
        static_cast<unsigned long>(control.config.scaleTimerStopExtraDelayMs),
        static_cast<unsigned long>(control.config.dripDelayMs),
        control.config.soundAlertsMuted ? "false" : "true",
        control.config.firstDropBeep ? "true" : "false",
        control.config.paddleReturnReminderBeep ? "true" : "false",
        static_cast<unsigned long>(
            control.config.paddleReturnReminderIntervalMs),
        static_cast<unsigned long>(
            control.config.paddleReturnReminderMaxDurationMs),
        paddleModeId(control.config.paddleMode),
        control.config.buzzerScaleLostBeep ? "true" : "false",
        control.config.buzzerAutoToManualGuardEndBeep ? "true" : "false",
        control.config.buzzerManualNoScaleBeep ? "true" : "false",
        control.config.buzzerScaleConnectedBeep ? "true" : "false",
        extendedPulseRateId(control.config.buzzerExtendedPulseRate),
        extendedPulseRateId(control.config.buzzerSlowExtendedPulseRate),
        alertOutputChannelId(control.config.alertOutputChannel),
        static_cast<unsigned long>(control.config.rinseGestureMs),
        static_cast<unsigned long>(control.config.rinseDurationMs),
        control.config.autoRetare ? "true" : "false",
        static_cast<unsigned long>(control.config.retareWindowMs),
        static_cast<double>(control.config.minimumCupWeightG),
        static_cast<unsigned>(control.config.retareStabilitySamples),
        static_cast<double>(control.config.retareStabilityToleranceG),
        static_cast<unsigned long>(control.config.retareStabilityMaxGapMs),
        static_cast<unsigned long>(control.config.retareStabilityMinDurationMs),
        static_cast<unsigned long>(control.config.bbwProtectionMs),
        static_cast<unsigned long>(control.config.operationalWallMs),
        control.config.fastExtractionGuardEnabled ? "true" : "false",
        static_cast<double>(control.config.maxRecoveryWeightG),
        static_cast<unsigned long>(control.config.minBrewTimeMs),
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<unsigned long>(control.config.maxBrewTimeMs),
        control.config.autoToManualGuardEnabled ? "true" : "false",
        autoToManualGuardLimitModeId(control.config.autoToManualGuardLimitMode),
        static_cast<unsigned long>(control.config.autoToManualGuardManualLimitMs),
        static_cast<unsigned long>(control.config.autoToManualGuardBaselineMs),
        static_cast<unsigned long>(control.autoToManualGuardTrendMs),
        scaleMacCacheModeId(control.config.scaleMacCacheMode),
        control.config.bookooMuteOnBuzzerOnly ? "true" : "false",
        static_cast<unsigned>(control.config.bookooConnectBeepLevel),
        control.config.avoidBbwShotWithoutScale ? "true" : "false",
        static_cast<unsigned long>(control.config.lastShotCooldownMs));
  }

  if (ok) {
    ok = statusJsonAppend(&used, "}");
  }

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"stateLabel\":\"%s\",\"relayClosed\":%s,"
        "\"physicalPaddleOn\":%s,\"virtualPaddleOn\":%s,"
        "\"remoteControlEnabled\":%s,\"controlSource\":\"%s\","
        "\"cn9ElapsedMs\":%lu,"
        "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
        "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
        "\"recoveryRequired\":%s},"
        "\"scale\":{\"available\":%s,\"protocol\":\"%s\","
        "\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"controlAccepted\":%s,\"currentWeightG\":%s,"
        "\"observedWeightG\":%s,\"timerMs\":%s,"
        "\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu},"
        "\"presets\":%s,"
        "\"cycle\":{\"active\":%s,\"shotType\":\"%s\","
        "\"retarePerformed\":%s,\"firstDropElapsedMs\":%lu,"
        "\"extractionExtended\":%s,\"slowExtractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"minBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu},"
        "\"lastShot\":{\"valid\":%s,\"currentWeightG\":%s,"
        "\"goalWeightG\":%u,\"extractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"durationMs\":%lu,"
        "\"firstDropElapsedMs\":%lu,\"retarePerformed\":%s,"
        "\"shotType\":\"%s\",\"scaleProtocol\":\"%s\","
        "\"scaleAvailable\":%s,\"fastExtractionGuardEnabled\":%s,"
        "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
        "\"minBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu,"
        "\"noScaleShotGuardEnabled\":%s,"
        "\"noScaleShotGuardArmed\":%s},"
        "\"noScaleShotGuard\":{\"enabled\":%s,\"armed\":%s}",
        stopperStateName(control.state), stateLabel(control.state),
        control.relayClosed ? "true" : "false",
        control.physicalPaddleOn ? "true" : "false",
        control.virtualPaddleOn ? "true" : "false",
        control.remoteControlEnabled ? "true" : "false",
        controlSourceName(control.source),
        static_cast<unsigned long>(control.cn9ElapsedMs),
        relaySafetyStateName(control.safetyState),
        relaySafetyFaultName(control.safetyFault),
        control.taskWatchdogReady ? "true" : "false",
        control.externalSafetyPresent ? "true" : "false",
        control.resetRecoveryRequired ? "true" : "false",
        control.scaleAvailable ? "true" : "false", safeScaleProtocol,
        weightStreamStateName(control.weightStreamState),
        weightControlStateName(control.weightControlState),
        control.currentWeightValid ? "true" : "false", currentWeight,
        observedWeight, scaleTimer, safePreferredScaleMac,
        safePreferredScaleName,
        static_cast<unsigned long>(control.scaleMacCachePauseRemainingMs),
        g_work->presetsJson, control.activeCycle ? "true" : "false",
        activeCycleShotTypeLabel(control),
        control.cycleRetarePerformed ? "true" : "false",
        static_cast<unsigned long>(cycleFirstDropElapsedMs),
        control.cycleExtractionExtended ? "true" : "false",
        control.cycleSlowExtractionExtended ? "true" : "false",
        static_cast<double>(control.cycleActiveStopWeightG),
        static_cast<unsigned long>(control.cycleMinBrewTimeRemainingMs),
        control.cycleAutoToManualGuardArmed ? "true" : "false",
        control.cycleAutoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(control.cycleAutoToManualGuardRemainingMs),
        control.lastShot.valid ? "true" : "false", lastShotWeight,
        static_cast<unsigned>(control.lastShot.goalWeightG),
        control.lastShot.extractionExtended ? "true" : "false",
        static_cast<double>(control.lastShot.activeStopWeightG),
        static_cast<unsigned long>(control.lastShot.durationMs),
        static_cast<unsigned long>(control.lastShot.firstDropElapsedMs),
        control.lastShot.retarePerformed ? "true" : "false",
        lastShotTypeName(static_cast<LastShotType>(control.lastShot.shotType)),
        safeLastShotProtocol,
        control.lastShot.scaleAvailable ? "true" : "false",
        control.lastShot.fastExtractionGuardEnabled ? "true" : "false",
        control.lastShot.slowExtractionGuardEnabled ? "true" : "false",
        control.lastShot.slowExtractionExtended ? "true" : "false",
        static_cast<unsigned long>(control.lastShot.minBrewTimeRemainingMs),
        control.lastShot.autoToManualGuardEnabled ? "true" : "false",
        control.lastShot.autoToManualGuardArmed ? "true" : "false",
        control.lastShot.autoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(
            control.lastShot.autoToManualGuardRemainingMs),
        control.lastShot.noScaleShotGuardEnabled ? "true" : "false",
        control.lastShot.noScaleShotGuardArmed ? "true" : "false",
        control.noScaleShotGuardEnabled ? "true" : "false",
        control.noScaleShotGuardArmed ? "true" : "false");
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"scale\":{\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,\"history\":%s},"
        "\"presets\":%s",
        safePreferredScaleMac, safePreferredScaleName,
        static_cast<unsigned long>(control.scaleMacCachePauseRemainingMs),
        g_work->historyJson, g_work->presetsJson);
  } else if (ok && page == StatusPage::Admin) {
    // Admin page: Wi-Fi/AP status + STA address form hydration only.
    // Diagnostics metrics live on status/diagnostic.
    ok = statusJsonAppend(
        &used,
        ",\"network\":{\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
        "\"wifiConfigured\":%s,\"ssid\":\"%s\",\"open\":%s,"
        "\"staState\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
        "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
        "\"rssi\":%s,\"signalQualityPct\":%s,"
        "\"configuredIp\":\"%s\",\"configuredNetmask\":\"%s\","
        "\"configuredGateway\":\"%s\",\"configuredDns1\":\"%s\","
        "\"configuredDns2\":\"%s\"},"
        "\"bleCompanion\":{\"enabled\":%s,\"active\":%s,"
        "\"restartRequired\":%s,\"stackReady\":%s,"
        "\"advertising\":%s,\"connected\":%s,\"protocolVersion\":%u,"
        "\"acceptedWrites\":%lu,\"rejectedWrites\":%lu,"
        "\"lastReject\":\"%s\"}",
        network.apActive ? "true" : "false", network.apIp,
        static_cast<unsigned>(network.apClients),
        network.wifiConfigured ? "true" : "false", safeStaSsid,
        network.staOpen ? "true" : "false", staStateName(network.staState),
        staChannelJson, network.staIp, staIpModeName(network.staIpMode),
        staConfigStateName(network.staConfigState),
        static_cast<unsigned long>(network.confirmRemainingMs), staRssiJson,
        staSignalQualityJson, network.configuredIp, network.configuredNetmask,
        network.configuredGateway, network.configuredDns1,
        network.configuredDns2,
        control.bleCompanionEnabled ? "true" : "false",
        control.bleCompanionActive ? "true" : "false",
        control.bleCompanionRestartRequired ? "true" : "false",
        control.bleCompanionStackReady ? "true" : "false",
        control.bleCompanionAdvertising ? "true" : "false",
        control.bleCompanionConnected ? "true" : "false",
        static_cast<unsigned>(control.bleCompanionProtocolVersion),
        static_cast<unsigned long>(control.bleCompanionAcceptedWrites),
        static_cast<unsigned long>(control.bleCompanionRejectedWrites),
        bleCompanionRejectReasonName(static_cast<BleCompanionRejectReason>(
            control.bleCompanionLastReject)));
  } else if (ok && page == StatusPage::Diagnostic) {
    // Lean diagnostic snapshot: metrics + log controls only (no STA address
    // form fields; those stay on status/admin).
    ok = statusJsonAppend(
        &used,
        ",\"network\":{\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
        "\"wifiConfigured\":%s,\"ssid\":\"%s\","
        "\"staState\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
        "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
        "\"rssi\":%s,\"signalQualityPct\":%s},"
        "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
        "\"nextRetryInMs\":%lu,\"activeServer\":\"%s\"},"
        "\"maintenance\":{\"active\":%s,\"leaseId\":%lu,"
        "\"persistPending\":%s,\"persistFailed\":%s},"
        "\"health\":{\"uptimeMs\":%lu,\"loopMaxGapMs\":%lu,"
        "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
        "\"largestFreeHeapBlockBytes\":%lu,"
        "\"psramSizeBytes\":%lu,\"psramFreeBytes\":%lu,"
        "\"psramLargestFreeBlockBytes\":%lu,"
        "\"hwmon\":{\"cpuLoad5s\":%.2f,\"cpuLoad1m\":%.2f,\"cpuLoad5m\":%.2f,"
        "\"cpu0Busy\":%.2f,\"cpu1Busy\":%.2f,\"cpuLoadValid\":%s,"
        "\"tempValid\":%s,\"tempC\":%.1f,\"tempPeakC\":%.1f,"
        "\"ramTotalBytes\":%lu,\"ramUsedBytes\":%lu,"
        "\"ramFreeBytes\":%lu}},"
        "\"safety\":{\"resetReasonCode\":%lu},"
        "\"scale\":{\"packetGaps\":%lu,\"rejectedPackets\":%lu,"
        "\"reconnects\":%lu,\"lastDisconnectReasonName\":\"%s\","
        "\"eventsDropped\":%lu},"
        "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"}",
        network.apActive ? "true" : "false", network.apIp,
        static_cast<unsigned>(network.apClients),
        network.wifiConfigured ? "true" : "false", safeStaSsid,
        staStateName(network.staState), staChannelJson, network.staIp,
        staIpModeName(network.staIpMode),
        staConfigStateName(network.staConfigState),
        static_cast<unsigned long>(network.confirmRemainingMs), staRssiJson,
        staSignalQualityJson, timeSyncStateName(timeStatus.state),
        static_cast<unsigned long>(timeStatus.utcSec),
        static_cast<unsigned long>(timeStatus.lastSyncAgeMs),
        static_cast<unsigned long>(timeStatus.nextRetryInMs), safeActiveServer,
        control.maintenanceLeaseActive ? "true" : "false",
        static_cast<unsigned long>(control.maintenanceLeaseId),
        control.configPersistPending ? "true" : "false",
        control.configPersistFailed ? "true" : "false",
        static_cast<unsigned long>(control.uptimeMs),
        static_cast<unsigned long>(control.loopMaxGapMs),
        static_cast<unsigned long>(control.freeHeapBytes),
        static_cast<unsigned long>(control.minimumFreeHeapBytes),
        static_cast<unsigned long>(control.largestFreeHeapBlockBytes),
        static_cast<unsigned long>(control.psramSizeBytes),
        static_cast<unsigned long>(control.psramFreeBytes),
        static_cast<unsigned long>(control.psramLargestFreeBlockBytes),
        static_cast<double>(control.hwmon.cpuLoad5s),
        static_cast<double>(control.hwmon.cpuLoad1m),
        static_cast<double>(control.hwmon.cpuLoad5m),
        static_cast<double>(control.hwmon.cpu0Busy),
        static_cast<double>(control.hwmon.cpu1Busy),
        control.hwmon.cpuLoadValid ? "true" : "false",
        control.hwmon.tempValid ? "true" : "false",
        static_cast<double>(control.hwmon.tempC),
        static_cast<double>(control.hwmon.tempPeakC),
        static_cast<unsigned long>(control.hwmon.ramTotalBytes),
        static_cast<unsigned long>(control.hwmon.ramUsedBytes),
        static_cast<unsigned long>(control.hwmon.ramFreeBytes),
        static_cast<unsigned long>(control.resetReasonCode),
        static_cast<unsigned long>(control.scalePacketGaps),
        static_cast<unsigned long>(control.scaleRejectedPackets),
        static_cast<unsigned long>(control.scaleReconnects),
        scaleDisconnectReasonName(control.scaleLastDisconnectReason),
        static_cast<unsigned long>(control.scaleEventsDropped),
        static_cast<unsigned long>(network.lastCommandRequestId),
        commandResultStateName(network.lastCommandState));
  }

  if (ok) {
    ok = statusJsonAppend(&used, "}");
  }

  if (!ok) {
    const esp_err_t tooLarge =
        sendError(request, "500 Internal Server Error", "STATUS_TOO_LARGE",
                  "Status snapshot exceeds its size limit.");
    self.unlockWorkBuf();
    return tooLarge;
  }
  const esp_err_t sent =
      sendJson(request, STATUS_OK, g_work->statusJson);
  self.unlockWorkBuf();
  return sent;
}

esp_err_t ShotStopperNetwork::logHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  // The bounded diagnostic log is intentionally public like Status. It emits
  // only fixed enum-derived messages and numeric arguments; credentials and
  // request payloads are never included.
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

  if (!self.lockWorkBuf()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  const size_t count = self.callbacks_.copyDebugEvents(
      after, work.logBatch, LOG_BATCH_SIZE);
  self.callbacks_.copyControlStatus(work.control);
  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  char header[96] = {};
  snprintf(header, sizeof(header),
           "{\"dropped\":%lu,\"bootId\":%lu,\"events\":[",
           static_cast<unsigned long>(work.control.debugEventsDropped),
           static_cast<unsigned long>(work.control.bootId));
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  for (size_t index = 0; index < count; ++index) {
    const DebugEvent &event = work.logBatch[index];
    char message[128] = {};
    if (event.code == DebugCode::BOOT_BANNER) {
      snprintf(message, sizeof(message), "Shot Stopper Micra %s (bootId=%ld)",
               FW_VERSION, static_cast<long>(event.argument1));
    } else if (event.code == DebugCode::STATE_TRANSITION &&
        event.argument1 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        event.argument1 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
        event.argument2 >=
            static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
        event.argument2 <=
            static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
      snprintf(message, sizeof(message), "%s -> %s",
               stopperStateName(static_cast<StopperState>(event.argument1)),
               stopperStateName(static_cast<StopperState>(event.argument2)));
    } else if ((event.code == DebugCode::WEB_COMMAND_ACCEPTED ||
                event.code == DebugCode::WEB_COMMAND_REJECTED) &&
               event.argument1 >=
                   static_cast<int32_t>(WebCommandType::PADDLE_ON) &&
               event.argument1 <=
                   static_cast<int32_t>(
                       WebCommandType::MAINTENANCE_COMPLETE)) {
      snprintf(message, sizeof(message), "%s: %s", debugCodeName(event.code),
               webCommandTypeName(
                   static_cast<WebCommandType>(event.argument1)));
    } else if (formatScaleSampleDebugMessage(event, message,
                                             sizeof(message))) {
    } else if (formatPersistDebugMessage(event, message, sizeof(message))) {
    } else if (formatLifecycleDebugMessage(event, message, sizeof(message))) {
    } else {
      strncpy(message, debugCodeName(event.code), sizeof(message) - 1);
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             "%s{\"sequence\":%lu,\"atMs\":%lu,\"wallSec\":%lu,"
             "\"level\":\"%s\",\"category\":\"%s\",\"code\":%u,"
             "\"message\":\"%s\",\"argument1\":%ld,\"argument2\":%ld}",
             index == 0 ? "" : ",",
             static_cast<unsigned long>(event.sequence),
             static_cast<unsigned long>(event.atMs),
             static_cast<unsigned long>(event.wallSec),
             logLevelName(event.level), debugCategoryName(event.category),
             static_cast<unsigned>(event.code), message,
             static_cast<long>(event.argument1),
             static_cast<long>(event.argument2));
    if (httpd_resp_send_chunk(request, work.jsonItem, HTTPD_RESP_USE_STRLEN) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  const bool ok =
      httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK &&
      httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
  self.unlockWorkBuf();
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.lockWorkBuf()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  self.callbacks_.copyControlStatus(work.control);
  const size_t count =
      self.callbacks_.copyShotRecords != nullptr
          ? self.callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY)
          : 0;

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");  
  char header[96] = {};
  snprintf(header, sizeof(header),
           "{\"bootId\":%lu,\"shots\":[",
           static_cast<unsigned long>(work.control.bootId));
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }

  for (size_t index = 0; index < count; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    char actual[16] = "null";
    char errorG[16] = "null";
    char errorPct[16] = "null";
    char flow[16] = "null";
    char firstDrop[16] = "null";
    char maxRecovery[16] = "null";
    char minBrewTime[16] = "null";
    char targetEarly[16] = "null";
    if (!shotLogWeightIsMissing(record.actualWeightCg)) {
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
    if (!shotLogWeightIsMissing(record.maxRecoveryWeightCg)) {
      snprintf(maxRecovery, sizeof(maxRecovery), "%.2f",
               static_cast<double>(record.maxRecoveryWeightCg) / 100.0);
    }
    if (record.minBrewTimeDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(minBrewTime, sizeof(minBrewTime), "%.1f",
               static_cast<double>(record.minBrewTimeDs) / 10.0);
    }
    if (record.targetReachedEarlyDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(targetEarly, sizeof(targetEarly), "%.1f",
               static_cast<double>(record.targetReachedEarlyDs) / 10.0);
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             "%s{\"id\":%lu,\"bootId\":%lu,\"endedAtMs\":%lu,"
             "\"hasWallTime\":%s,\"endedAtLocalSec\":%lu,"
             "\"endedAtUnixSec\":%lu,\"timezoneOffsetMinutesAtCommit\":%d,"
             "\"durationS\":%.1f,"
             "\"goalG\":%u,\"actualG\":%s,\"errorG\":%s,\"errorPct\":%s,"
             "\"offsetG\":%.2f,\"avgFlowGS\":%s,\"firstDropS\":%s,"
             "\"shotType\":\"%s\",\"cutType\":\"%s\","
             "\"extractionGuardEnabled\":%s,\"extractionExtended\":%s,"
             "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
             "\"stopDetail\":\"%s\",\"maxRecoveryWeightG\":%s,"
             "\"minBrewTimeS\":%s,\"targetReachedEarlyS\":%s,"
             "\"actualWeightSource\":\"%s\"}",
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
             shotLogCutName(static_cast<ShotLogCut>(record.cutType)),
             shotLogFastGuardEnabled(record.extractionGuardEnabled) ? "true"
                                                                    : "false",
             shotLogFastExtended(record.extractionExtended) ? "true" : "false",
             shotLogSlowGuardEnabled(record.extractionGuardEnabled) ? "true"
                                                                    : "false",
             shotLogSlowExtended(record.extractionExtended) ? "true" : "false",
             shotLogStopDetailName(
                 static_cast<ShotLogStopDetail>(record.stopDetail)),
             maxRecovery, minBrewTime, targetEarly,
             actualWeightSourceName(
                 static_cast<ActualWeightSource>(record.actualWeightSource)));
    if (httpd_resp_send_chunk(request, work.jsonItem, HTTPD_RESP_USE_STRLEN) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  const bool ok =
      httpd_resp_send_chunk(request, "]}", HTTPD_RESP_USE_STRLEN) == ESP_OK &&
      httpd_resp_send_chunk(request, nullptr, 0) == ESP_OK;
  self.unlockWorkBuf();
  return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t ShotStopperNetwork::shotsClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
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

esp_err_t ShotStopperNetwork::lastShotClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before clearing the last shot.");
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
      strcmp(confirmation, "CLEAR_LAST_SHOT") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  memset(confirmation, 0, sizeof(confirmation));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE,
                     "LAST_SHOT_CLEAR_NOT_CONFIRMED",
                     "The last-shot clear was not explicitly confirmed.");
  }

  if (self.callbacks_.clearLastShot == nullptr ||
      !self.callbacks_.clearLastShot()) {
    return sendError(request, STATUS_UNAVAILABLE, "LAST_SHOT_CLEAR_FAILED",
                     "Last shot could not be cleared.");
  }
  return sendJson(request, STATUS_OK, "{\"cleared\":true}");
}

esp_err_t ShotStopperNetwork::shotsDeleteHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
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
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical paddle OFF, and wait for Ready before syncing the clock.");
  }
  self.ntpManualSyncPending_ = true;
  return self.sendAccepted(request, self.allocateRequestId());
}

esp_err_t ShotStopperNetwork::configHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
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
  // Patch: seed live effective config; only present keys overwrite.
  RuntimeConfig candidate = status.config;
  bool brewByWeightPresent = false;
  bool brewByWeight = !candidate.timerOnly;
  bool baseRevisionPresent = false;
  uint32_t baseRevision = 0;
  bool soundAlertsEnabled = !candidate.soundAlertsMuted;
  char customNtp[NTP_SERVER_HOST_CAPACITY] = {};
  memcpy(customNtp, candidate.ntpServerCustom, sizeof(customNtp));
  static const char *const fields[] = {
      "baseRevision", "goalWeightG", "rinseGestureMs", "rinseDurationMs",
      "operationalWallMs", "autoTare", "brewByWeight", "canTareStartTimer",
      "scaleTimerStopExtraDelayMs", "dripDelayMs", "soundAlertsEnabled",
      "firstDropBeep",
      "paddleReturnReminderBeep",
      "paddleReturnReminderIntervalMs", "paddleReturnReminderMaxDurationMs",
      "paddleMode", "buzzerScaleLostBeep", "buzzerAutoToManualGuardEndBeep",
      "buzzerManualNoScaleBeep", "buzzerScaleConnectedBeep",
      "buzzerExtendedPulseRate", "buzzerSlowExtendedPulseRate",
      "alertOutputChannel", "autoRetare", "retareWindowMs", "minimumCupWeightG",
      "retareStabilitySamples", "retareStabilityToleranceG",
      "retareStabilityMaxGapMs", "retareStabilityMinDurationMs",
      "bbwProtectionMs", "fastExtractionGuardEnabled", "maxRecoveryWeightG",
      "minBrewTimeMs", "slowExtractionGuardEnabled", "minRecoveryWeightG",
      "maxBrewTimeMs", "autoToManualGuardEnabled", "autoToManualGuardLimitMode",
      "autoToManualGuardManualLimitMs", "autoToManualGuardBaselineMs",
      "weightOffsetBaselineG", "timezoneOffsetMinutes", "ntpServerPreset",
      "ntpServerCustom", "scaleMacCacheMode", "bookooMuteOnBuzzerOnly",
      "bookooConnectBeepLevel", "avoidBbwShotWithoutScale", "lastShotCooldownMs",
      "serialDebugOutput", "ringRetainLogLevel"};
  const char *parseError = nullptr;
  size_t settingFieldCount = 0;
  if (root != nullptr) {
    for (cJSON *item = root->child; item != nullptr; item = item->next) {
      if (item->string != nullptr &&
          strcmp(item->string, "baseRevision") != 0) {
        ++settingFieldCount;
      }
    }
  }
  if (root == nullptr ||
      !jsonHasOnlyUniqueFields(root, fields,
                               sizeof(fields) / sizeof(fields[0]))) {
    parseError =
        "Config must be a JSON object with known fields and correct types.";
  } else if (settingFieldCount == 0) {
    parseError = "Config patch must include at least one setting field.";
  } else if (jsonFieldPresent(root, "baseRevision") &&
             !jsonUint32(root, "baseRevision", baseRevision)) {
    parseError = "baseRevision must be an integer.";
  } else if (jsonFieldPresent(root, "goalWeightG") &&
             !jsonUint8(root, "goalWeightG", candidate.goalWeightG)) {
    parseError = "goalWeightG must be an integer from 10 to 200.";
  } else if (jsonFieldPresent(root, "rinseGestureMs") &&
             !jsonUint32(root, "rinseGestureMs", candidate.rinseGestureMs)) {
    parseError = "rinseGestureMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "rinseDurationMs") &&
             !jsonUint32(root, "rinseDurationMs", candidate.rinseDurationMs)) {
    parseError = "rinseDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "operationalWallMs") &&
             !jsonUint32(root, "operationalWallMs",
                         candidate.operationalWallMs)) {
    parseError = "operationalWallMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoTare") &&
             !jsonBoolean(root, "autoTare", candidate.autoTare)) {
    parseError = "autoTare must be a boolean.";
  } else if (jsonFieldPresent(root, "brewByWeight") &&
             !jsonBoolean(root, "brewByWeight", brewByWeight)) {
    parseError = "brewByWeight must be a boolean.";
  } else if (jsonFieldPresent(root, "canTareStartTimer") &&
             !jsonBoolean(root, "canTareStartTimer",
                          candidate.canTareStartTimer)) {
    parseError = "canTareStartTimer must be a boolean.";
  } else if (jsonFieldPresent(root, "scaleTimerStopExtraDelayMs") &&
             !jsonUint32(root, "scaleTimerStopExtraDelayMs",
                         candidate.scaleTimerStopExtraDelayMs)) {
    parseError = "scaleTimerStopExtraDelayMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "dripDelayMs") &&
             !jsonUint32(root, "dripDelayMs", candidate.dripDelayMs)) {
    parseError = "dripDelayMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "soundAlertsEnabled") &&
             !jsonBoolean(root, "soundAlertsEnabled", soundAlertsEnabled)) {
    parseError = "soundAlertsEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "firstDropBeep") &&
             !jsonBoolean(root, "firstDropBeep", candidate.firstDropBeep)) {
    parseError = "firstDropBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "paddleReturnReminderBeep") &&
             !jsonBoolean(root, "paddleReturnReminderBeep",
                          candidate.paddleReturnReminderBeep)) {
    parseError = "paddleReturnReminderBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "paddleReturnReminderIntervalMs") &&
             !jsonUint32(root, "paddleReturnReminderIntervalMs",
                         candidate.paddleReturnReminderIntervalMs)) {
    parseError =
        "paddleReturnReminderIntervalMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "paddleReturnReminderMaxDurationMs") &&
             !jsonUint32(root, "paddleReturnReminderMaxDurationMs",
                         candidate.paddleReturnReminderMaxDurationMs)) {
    parseError =
        "paddleReturnReminderMaxDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "paddleMode") &&
             !jsonPaddleMode(root, "paddleMode", candidate.paddleMode)) {
    parseError = "paddleMode must be auto, natural or original.";
  } else if (jsonFieldPresent(root, "buzzerScaleLostBeep") &&
             !jsonBoolean(root, "buzzerScaleLostBeep",
                          candidate.buzzerScaleLostBeep)) {
    parseError = "buzzerScaleLostBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerAutoToManualGuardEndBeep") &&
             !jsonBoolean(root, "buzzerAutoToManualGuardEndBeep",
                          candidate.buzzerAutoToManualGuardEndBeep)) {
    parseError = "buzzerAutoToManualGuardEndBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerManualNoScaleBeep") &&
             !jsonBoolean(root, "buzzerManualNoScaleBeep",
                          candidate.buzzerManualNoScaleBeep)) {
    parseError = "buzzerManualNoScaleBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerScaleConnectedBeep") &&
             !jsonBoolean(root, "buzzerScaleConnectedBeep",
                          candidate.buzzerScaleConnectedBeep)) {
    parseError = "buzzerScaleConnectedBeep must be a boolean.";
  } else if (jsonFieldPresent(root, "buzzerExtendedPulseRate") &&
             !jsonExtendedPulseRate(root, "buzzerExtendedPulseRate",
                                    candidate.buzzerExtendedPulseRate)) {
    parseError =
        "buzzerExtendedPulseRate must be disabled, slow, medium, fast, or rapid.";
  } else if (jsonFieldPresent(root, "buzzerSlowExtendedPulseRate") &&
             !jsonExtendedPulseRate(root, "buzzerSlowExtendedPulseRate",
                                    candidate.buzzerSlowExtendedPulseRate)) {
    parseError =
        "buzzerSlowExtendedPulseRate must be disabled, slow, medium, fast, or "
        "rapid.";
  } else if (jsonFieldPresent(root, "alertOutputChannel") &&
             !jsonAlertOutputChannel(root, "alertOutputChannel",
                                     candidate.alertOutputChannel,
                                     /*optional=*/false)) {
    parseError =
        "alertOutputChannel must be scale_only, buzzer_only, or scale_priority.";
  } else if (jsonFieldPresent(root, "autoRetare") &&
             !jsonBoolean(root, "autoRetare", candidate.autoRetare)) {
    parseError = "autoRetare must be a boolean.";
  } else if (jsonFieldPresent(root, "retareWindowMs") &&
             !jsonUint32(root, "retareWindowMs", candidate.retareWindowMs)) {
    parseError = "retareWindowMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "minimumCupWeightG") &&
             !jsonFloat(root, "minimumCupWeightG",
                        candidate.minimumCupWeightG)) {
    parseError = "minimumCupWeightG must be a number.";
  } else if (jsonFieldPresent(root, "retareStabilitySamples") &&
             !jsonUint8(root, "retareStabilitySamples",
                        candidate.retareStabilitySamples)) {
    parseError = "retareStabilitySamples must be an integer from 2 to 10.";
  } else if (jsonFieldPresent(root, "retareStabilityToleranceG") &&
             !jsonFloat(root, "retareStabilityToleranceG",
                        candidate.retareStabilityToleranceG)) {
    parseError = "retareStabilityToleranceG must be a number.";
  } else if (jsonFieldPresent(root, "retareStabilityMaxGapMs") &&
             !jsonUint32(root, "retareStabilityMaxGapMs",
                         candidate.retareStabilityMaxGapMs)) {
    parseError = "retareStabilityMaxGapMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "retareStabilityMinDurationMs") &&
             !jsonUint32(root, "retareStabilityMinDurationMs",
                         candidate.retareStabilityMinDurationMs)) {
    parseError =
        "retareStabilityMinDurationMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "bbwProtectionMs") &&
             !jsonUint32(root, "bbwProtectionMs", candidate.bbwProtectionMs)) {
    parseError = "bbwProtectionMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "fastExtractionGuardEnabled") &&
             !jsonBoolean(root, "fastExtractionGuardEnabled",
                          candidate.fastExtractionGuardEnabled)) {
    parseError = "fastExtractionGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "maxRecoveryWeightG") &&
             !jsonFloat(root, "maxRecoveryWeightG",
                        candidate.maxRecoveryWeightG)) {
    parseError = "maxRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "minBrewTimeMs") &&
             !jsonUint32(root, "minBrewTimeMs", candidate.minBrewTimeMs)) {
    parseError = "minBrewTimeMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "slowExtractionGuardEnabled") &&
             !jsonBoolean(root, "slowExtractionGuardEnabled",
                          candidate.slowExtractionGuardEnabled)) {
    parseError = "slowExtractionGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "minRecoveryWeightG") &&
             !jsonFloat(root, "minRecoveryWeightG",
                        candidate.minRecoveryWeightG)) {
    parseError = "minRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "maxBrewTimeMs") &&
             !jsonUint32(root, "maxBrewTimeMs", candidate.maxBrewTimeMs)) {
    parseError = "maxBrewTimeMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoToManualGuardEnabled") &&
             !jsonBoolean(root, "autoToManualGuardEnabled",
                          candidate.autoToManualGuardEnabled)) {
    parseError = "autoToManualGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "autoToManualGuardLimitMode") &&
             !jsonAutoToManualGuardLimitMode(
                 root, "autoToManualGuardLimitMode",
                 candidate.autoToManualGuardLimitMode)) {
    parseError = "autoToManualGuardLimitMode must be \"manual\" or \"auto\".";
  } else if (jsonFieldPresent(root, "autoToManualGuardManualLimitMs") &&
             !jsonUint32(root, "autoToManualGuardManualLimitMs",
                         candidate.autoToManualGuardManualLimitMs)) {
    parseError =
        "autoToManualGuardManualLimitMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "autoToManualGuardBaselineMs") &&
             !jsonUint32(root, "autoToManualGuardBaselineMs",
                         candidate.autoToManualGuardBaselineMs)) {
    parseError =
        "autoToManualGuardBaselineMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "weightOffsetBaselineG") &&
             !jsonFloat(root, "weightOffsetBaselineG",
                        candidate.weightOffsetBaselineG)) {
    parseError = "weightOffsetBaselineG must be a number.";
  } else if (jsonFieldPresent(root, "timezoneOffsetMinutes") &&
             !jsonInt16(root, "timezoneOffsetMinutes",
                        candidate.timezoneOffsetMinutes)) {
    parseError = "timezoneOffsetMinutes must be an integer.";
  } else if (jsonFieldPresent(root, "ntpServerPreset") &&
             !jsonNtpPreset(root, "ntpServerPreset",
                            candidate.ntpServerPreset)) {
    parseError = "ntpServerPreset must be pool, google, cloudflare, or nist.";
  } else if (jsonFieldPresent(root, "ntpServerCustom") &&
             !jsonString(root, "ntpServerCustom", customNtp, sizeof(customNtp),
                         true)) {
    parseError = "ntpServerCustom must be a string of at most 63 characters.";
  } else if (jsonFieldPresent(root, "scaleMacCacheMode") &&
             !jsonScaleMacCacheMode(root, "scaleMacCacheMode",
                                    candidate.scaleMacCacheMode)) {
    parseError = "scaleMacCacheMode must be disabled or full.";
  } else if (jsonFieldPresent(root, "bookooMuteOnBuzzerOnly") &&
             !jsonBoolean(root, "bookooMuteOnBuzzerOnly",
                          candidate.bookooMuteOnBuzzerOnly)) {
    parseError = "bookooMuteOnBuzzerOnly must be a boolean.";
  } else if (jsonFieldPresent(root, "bookooConnectBeepLevel") &&
             (!jsonUint8(root, "bookooConnectBeepLevel",
                         candidate.bookooConnectBeepLevel) ||
              candidate.bookooConnectBeepLevel > BOOKOO_BEEP_LEVEL_MAX)) {
    parseError = "bookooConnectBeepLevel must be an integer from 0 to 5.";
  } else if (jsonFieldPresent(root, "avoidBbwShotWithoutScale") &&
             !jsonBoolean(root, "avoidBbwShotWithoutScale",
                          candidate.avoidBbwShotWithoutScale)) {
    parseError = "avoidBbwShotWithoutScale must be a boolean.";
  } else if (jsonFieldPresent(root, "lastShotCooldownMs") &&
             !jsonUint32(root, "lastShotCooldownMs",
                         candidate.lastShotCooldownMs)) {
    parseError = "lastShotCooldownMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "serialDebugOutput") &&
             !jsonBoolean(root, "serialDebugOutput",
                          candidate.serialDebugOutput)) {
    parseError = "serialDebugOutput must be a boolean.";
  } else if (jsonFieldPresent(root, "ringRetainLogLevel") &&
             !jsonLogLevel(root, "ringRetainLogLevel",
                           candidate.ringRetainLogLevel)) {
    parseError =
        "ringRetainLogLevel must be none, critical, error, warning, info, or "
        "debug.";
  }
  if (root != nullptr) {
    brewByWeightPresent = jsonFieldPresent(root, "brewByWeight");
    baseRevisionPresent = jsonFieldPresent(root, "baseRevision");
    cJSON_Delete(root);
  }
  if (parseError != nullptr) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     parseError);
  }
  if (baseRevisionPresent && baseRevision != status.config.revision) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_CONFLICT, "CONFIG_REVISION_STALE",
                     "Config changed; refresh and retry.");
  }
  if (brewByWeightPresent) {
    candidate.timerOnly = !brewByWeight;
  }
  candidate.soundAlertsMuted = !soundAlertsEnabled;
  memcpy(candidate.ntpServerCustom, customNtp, sizeof(candidate.ntpServerCustom));
  memset(customNtp, 0, sizeof(customNtp));
  const RuntimeConfig effective =
      composeEffectiveConfig(candidate, status.presets);
  const ConfigValidationError error = validateRuntimeConfig(effective);
  if (error != ConfigValidationError::NONE) {
    self.log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED,
             static_cast<int32_t>(error));
    char errorBody[320] = {};
    snprintf(errorBody, sizeof(errorBody),
             "{\"error\":\"INVALID_CONFIG\",\"message\":\"%s\",\"field\":\"%s\"}",
             configValidationMessage(error), configValidationErrorName(error));
    return sendJson(request, STATUS_UNPROCESSABLE, errorBody);
  }
  WebCommand command;
  command.type = WebCommandType::APPLY_CONFIG;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.config = candidate;
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::preferredScaleClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The paired scale cannot be forgotten while a cycle "
                     "is active.");
  }
  WebCommand command;
  command.type = WebCommandType::CLEAR_PREFERRED_SCALE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was cleared.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::preferredScaleSelectHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The preferred scale cannot be changed while a cycle "
                     "is active.");
  }

  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A bounded JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  if (root == nullptr) {
    parseError = "JSON body is required.";
  } else if (!jsonString(root, "mac", mac, sizeof(mac), true)) {
    parseError = "mac must be a string (empty clears the preferred scale).";
  } else if (mac[0] != '\0' && !validPreferredScaleMac(mac)) {
    parseError = "mac must be empty or a BLE address AA:BB:CC:DD:EE:FF.";
  } else {
    cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (nameItem != nullptr) {
      if (!jsonString(root, "name", name, sizeof(name), true) ||
          !validPreferredScaleName(name)) {
        parseError = "name contains unsupported characters.";
      }
    }
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  if (parseError != nullptr) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST", parseError);
  }

  WebCommand command;
  command.type = WebCommandType::SELECT_PREFERRED_SCALE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  strncpy(command.scaleSelectMac, mac, sizeof(command.scaleSelectMac) - 1);
  strncpy(command.scaleSelectName, name, sizeof(command.scaleSelectName) - 1);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was selected.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::presetsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Presets are locked while a cycle is active.");
  }

  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A bounded JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char action[24] = {};
  uint8_t presetId = 0;
  char presetName[SHOT_PRESET_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  PresetAction presetAction = PresetAction::APPLY;
  WebCommand command;
  command.type = WebCommandType::PRESET_OP;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  command.config = status.config;

  if (root == nullptr || !jsonString(root, "action", action, sizeof(action),
                                     false)) {
    parseError = "action is required.";
  } else if (strcmp(action, "apply") == 0 || strcmp(action, "load") == 0) {
    presetAction = PresetAction::APPLY;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "create") == 0 || strcmp(action, "new") == 0) {
    presetAction = PresetAction::CREATE;
  } else if (strcmp(action, "delete") == 0) {
    presetAction = PresetAction::DELETE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "duplicate") == 0) {
    presetAction = PresetAction::DUPLICATE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "rename") == 0) {
    presetAction = PresetAction::RENAME;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    } else if (!jsonString(root, "name", presetName, sizeof(presetName),
                           false)) {
      parseError = "name is required.";
    }
  } else if (strcmp(action, "restore_factory_values") == 0) {
    presetAction = PresetAction::RESTORE_FACTORY_VALUES;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    }
  } else if (strcmp(action, "save") == 0 || strcmp(action, "update") == 0) {
    presetAction = PresetAction::SAVE;
    if (!jsonUint8(root, "id", presetId) || presetId == 0) {
      parseError = "id must be a non-zero integer.";
    } else {
      bool brewByWeight = true;
      if (!jsonBoolean(root, "brewByWeight", brewByWeight) ||
          !jsonUint8(root, "goalWeightG", command.config.goalWeightG) ||
          !jsonUint32(root, "operationalWallMs",
                      command.config.operationalWallMs) ||
          !jsonUint32(root, "bbwProtectionMs",
                      command.config.bbwProtectionMs) ||
          !jsonFloat(root, "weightOffsetBaselineG",
                     command.config.weightOffsetBaselineG) ||
          !jsonBoolean(root, "fastExtractionGuardEnabled",
                       command.config.fastExtractionGuardEnabled) ||
          !jsonFloat(root, "maxRecoveryWeightG",
                     command.config.maxRecoveryWeightG) ||
          !jsonUint32(root, "minBrewTimeMs", command.config.minBrewTimeMs) ||
          !jsonBoolean(root, "slowExtractionGuardEnabled",
                       command.config.slowExtractionGuardEnabled) ||
          !jsonFloat(root, "minRecoveryWeightG",
                     command.config.minRecoveryWeightG) ||
          !jsonUint32(root, "maxBrewTimeMs", command.config.maxBrewTimeMs) ||
          !jsonBoolean(root, "autoToManualGuardEnabled",
                       command.config.autoToManualGuardEnabled) ||
          !jsonAutoToManualGuardLimitMode(
              root, "autoToManualGuardLimitMode",
              command.config.autoToManualGuardLimitMode) ||
          !jsonUint32(root, "autoToManualGuardManualLimitMs",
                      command.config.autoToManualGuardManualLimitMs) ||
          !jsonUint32(root, "autoToManualGuardBaselineMs",
                      command.config.autoToManualGuardBaselineMs)) {
        parseError = "save requires the full Brew recipe field set.";
      } else {
        command.config.timerOnly = !brewByWeight;
      }
    }
  } else {
    parseError = "Unknown preset action.";
  }

  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (parseError != nullptr) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD", parseError);
  }

  command.presetAction = static_cast<uint8_t>(presetAction);
  command.presetId = presetId;
  strncpy(command.presetName, presetName, sizeof(command.presetName) - 1);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetCalibrationHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
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
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; calibration was not reset.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetGuardSamplesHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Guard samples are locked while a cycle is active.");
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
                     "The guard samples reset request must be an empty object.");
  }
  WebCommand command;
  command.type = WebCommandType::RESET_AUTO_TO_MANUAL_GUARD_SAMPLES;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; guard samples were not reset.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::paddleHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
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
  const bool allowed = on ? self.webUiConfigurationAllowed(request, status)
                          : (status.activeCycle &&
                             status.source == ControlSource::WEB);
  if (!allowed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "The current state does not allow that action.");
  }
  WebCommand command;
  command.type = on ? WebCommandType::PADDLE_ON : WebCommandType::PADDLE_OFF;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::rinseHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!REMOTE_CN9_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote CN9 actuation is disabled in this firmware build.");
  }
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "Rinse can only start from Ready.");
  }
  WebCommand command;
  command.type = WebCommandType::RINSE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::stopHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
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
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle and wait for Ready before restarting.");
  }
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::factoryResetHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
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
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; no settings were erased.");
  }
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::networkHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  char action[16] = {};
  WebCommand command;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  static const char *const forgetFields[] = {"action"};
  static const char *const confirmFields[] = {"action"};
  static const char *const saveFields[] = {
      "action", "ssid",     "password", "open",  "ipMode",
      "ip",     "netmask",  "gateway",  "dns1",  "dns2"};
  const char *networkError = nullptr;
  bool parsed = root != nullptr &&
                jsonString(root, "action", action, sizeof(action), false);
  if (parsed && strcmp(action, "confirm") == 0) {
    parsed = jsonHasOnlyUniqueFields(root, confirmFields, 1);
    if (!parsed) {
      networkError = "Confirm request must include only action=\"confirm\".";
    } else {
      const PersistedSettings settings = self.settingsCopy();
      const NetworkStatusSnapshot network = self.snapshot();
      if (settings.staConfigState !=
              static_cast<uint8_t>(StaConfigState::PENDING) ||
          !settings.staConfigured || network.apActive ||
          network.staState != StaState::CONNECTED) {
        parsed = false;
        networkError = "No pending network configuration to confirm.";
      } else {
        self.requestPendingNetworkConfirm();
        if (root != nullptr) {
          cJSON_Delete(root);
        }
        memset(body, 0, sizeof(body));
        return self.sendAccepted(request, command.requestId,
                                 "\"state\":\"QUEUED\"");
      }
    }
  } else if (parsed && strcmp(action, "forget") == 0) {
    if (!self.webUiConfigurationAllowed(request, status)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      memset(body, 0, sizeof(body));
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Network settings are locked while a cycle is active.");
    }
    parsed = jsonHasOnlyUniqueFields(root, forgetFields, 1);
    if (parsed) {
      command.type = WebCommandType::FORGET_NETWORK;
    } else {
      networkError = "Forget request must include only action=\"forget\".";
    }
  } else if (parsed && strcmp(action, "save") == 0) {
    if (!self.webUiConfigurationAllowed(request, status)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      memset(body, 0, sizeof(body));
      return sendError(request, STATUS_CONFLICT,
                       "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                       "Network settings are locked while a cycle is active.");
    }
    command.type = WebCommandType::SAVE_NETWORK;
    char ipMode[16] = {};
    char ipText[16] = {};
    char netmaskText[16] = {};
    char gatewayText[16] = {};
    char dns1Text[16] = {};
    char dns2Text[16] = {};
    const bool hasIp = cJSON_GetObjectItemCaseSensitive(root, "ip") != nullptr;
    const bool hasNetmask =
        cJSON_GetObjectItemCaseSensitive(root, "netmask") != nullptr;
    const bool hasGateway =
        cJSON_GetObjectItemCaseSensitive(root, "gateway") != nullptr;
    const bool hasDns1 =
        cJSON_GetObjectItemCaseSensitive(root, "dns1") != nullptr;
    const bool hasDns2 =
        cJSON_GetObjectItemCaseSensitive(root, "dns2") != nullptr;
    if (!jsonHasOnlyUniqueFields(root, saveFields, 10) ||
        !jsonString(root, "ssid", command.ssid, sizeof(command.ssid), false) ||
        !jsonString(root, "password", command.password,
                    sizeof(command.password), true) ||
        !jsonBoolean(root, "open", command.openNetwork) ||
        !jsonString(root, "ipMode", ipMode, sizeof(ipMode), false)) {
      parsed = false;
      networkError =
          "Save request requires action, ssid, password, open, and ipMode.";
    } else if (strcmp(ipMode, "dhcp") == 0) {
      command.staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
      if (hasIp || hasNetmask || hasGateway || hasDns1 || hasDns2) {
        parsed = false;
        networkError = "DHCP save must not include static address fields.";
      }
    } else if (strcmp(ipMode, "static") == 0) {
      command.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
      if (!hasIp || !hasNetmask || !hasGateway || !hasDns1 ||
          !jsonString(root, "ip", ipText, sizeof(ipText), false) ||
          !jsonString(root, "netmask", netmaskText, sizeof(netmaskText),
                      false) ||
          !jsonString(root, "gateway", gatewayText, sizeof(gatewayText),
                      false) ||
          !jsonString(root, "dns1", dns1Text, sizeof(dns1Text), false) ||
          (hasDns2 && !jsonString(root, "dns2", dns2Text, sizeof(dns2Text),
                                  false)) ||
          !parseIpv4(ipText, command.staIp) ||
          !parseIpv4(netmaskText, command.staNetmask) ||
          !parseIpv4(gatewayText, command.staGateway) ||
          !parseIpv4(dns1Text, command.staDns1) ||
          (hasDns2 && !parseIpv4(dns2Text, command.staDns2))) {
        parsed = false;
        networkError =
            "Static IP save requires valid ip, netmask, gateway, and dns1.";
      }
    } else {
      parsed = false;
      networkError = "ipMode must be \"dhcp\" or \"static\".";
    }
    if (parsed && !validWifiSsid(command.ssid)) {
      parsed = false;
      networkError = "SSID must be 1–32 characters.";
    } else if (parsed) {
      const PersistedSettings settings = self.settingsCopy();
      const bool reusePassword = shouldReuseSavedWifiCredentials(
          command.ssid, command.password, command.openNetwork,
          settings.staConfigured, settings.staSsid, settings.staOpen);
      if (!reusePassword &&
          !validWifiPassword(command.password, command.openNetwork)) {
        parsed = false;
        networkError = command.openNetwork
                           ? "Open network password must be empty."
                           : (settings.staConfigured
                                  ? "Wi-Fi password must be 8–63 characters, "
                                    "or empty to keep the saved password."
                                  : "Wi-Fi password must be 8–63 characters for a "
                                    "secured network.");
      } else if (!validStaAddressConfig(command.staIpMode, command.staIp,
                                        command.staNetmask, command.staGateway,
                                        command.staDns1, command.staDns2)) {
        parsed = false;
        networkError =
            "Static IP, netmask, gateway, and DNS must be valid and on the "
            "same subnet (not 192.168.4.x).";
      }
    }
  } else {
    parsed = false;
    networkError = "action must be \"save\", \"forget\", or \"confirm\".";
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_NETWORK",
                     networkError != nullptr
                         ? networkError
                         : "Invalid network SSID or password.");
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
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Wi-Fi scanning is available only while Ready.");
  }

  WebCommand command;
  command.type = WebCommandType::START_WIFI_SCAN;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; scan was not started.");
  }
  
  portENTER_CRITICAL(&self.dataMux_);
  self.scan_.state = WifiScanState::QUEUED;
  portEXIT_CRITICAL(&self.dataMux_);
  
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::wifiScanStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  WifiScanSnapshot scan;
  portENTER_CRITICAL(&self.dataMux_);
  scan = self.scan_;
  portEXIT_CRITICAL(&self.dataMux_);

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
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
  ControlStatusSnapshot status;
  self.callbacks_.copyControlStatus(status);
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The password cannot be changed while a cycle is active.");
  }
  char body[REQUEST_BODY_CAPACITY] = {};
  WebCommand command;
  command.type = WebCommandType::CHANGE_AP_PASSWORD;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  static const char *const fields[] = {"newPassword"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "newPassword", command.password,
                 sizeof(command.password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_AP_PASSWORD",
                     "A new password field is required.");
  }
  if (!validAccessPointPassword(command.password)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_AP_PASSWORD",
                     "New password must be 8–63 characters.");
  }
  if (isFactoryDefaultPassword(command.password)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_AP_PASSWORD",
                     "New password cannot be the factory default.");
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Nothing was saved.");
  }
  memset(command.password, 0, sizeof(command.password));
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::bleCompatHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  char body[REQUEST_BODY_CAPACITY] = {};
  if (!readJsonBody(request, body)) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     "A JSON request is required.");
  }
  cJSON *root = cJSON_Parse(body);
  bool enabled = false;
  static const char *const fields[] = {"enabled"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonBoolean(root, "enabled", enabled);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  memset(body, 0, sizeof(body));
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_BLE_CONFIG",
                     "enabled must be a boolean.");
  }
  WebCommand command;
  command.type = enabled ? WebCommandType::BLE_COMPAT_ENABLE
                         : WebCommandType::BLE_COMPAT_DISABLE;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "BLE Companion state was not changed.");
  }
  return self.sendAccepted(request, command.requestId);
}

}  // namespace shotstopper
