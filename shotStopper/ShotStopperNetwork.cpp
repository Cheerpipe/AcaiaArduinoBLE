#include "ShotStopperNetwork.h"
#include "ShotStopperDebugExport.h"
#include "ShotStopperMachineMomentaryConfig.h"
#include "ShotStopperMachinePaddleConfig.h"
#include "ShotStopperJsonArena.h"
#include "ShotStopperOta.h"
#include "ShotStopperPsram.h"
#include "ShotStopperRecovery.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperSerialCli.h"
#include "ShotStopperShotCurveTypes.h"
#include "ShotStopperVersion.h"
#include "ShotStopperWatchdog.h"

#include "ShotStopperWebAssetsGzip.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include "ShotStopperRfCoex.h"
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace shotstopper {

WallClock g_wallClock;

struct NetworkWorkBuf {
  static constexpr size_t kStatusJson = 12288;
  static constexpr size_t kPresetsJson = 2800;
  static constexpr size_t kHistoryJson = 1400;
  static constexpr size_t kJsonItem = 1800;
  // Worst case is ~493 B: the envelope with the longest state and lock reason,
  // plus two fully populated tags at OTA_ARCH_CAPACITY + OTA_VERSION_CAPACITY.
  static constexpr size_t kOtaJson = 640;
  char statusJson[kStatusJson]{};
  char presetsJson[kPresetsJson]{};
  char historyJson[kHistoryJson]{};
  char jsonItem[kJsonItem]{};
  char otaJson[kOtaJson]{};
  DebugEvent logBatch[kNetworkLogBatchSize]{};
  ShotLogRecord shotRecords[SHOT_LOG_CAPACITY]{};
  ShotCurveRecord shotCurves[SHOT_CURVE_CAPACITY]{};
  ControlStatusSnapshot control{};
  TaskProfilerSnapshot taskProfiler{};
  DebugExportExtras debugExport{};
  ShotPresetBank presetBank{};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY]{};
  BullseyeMelodyConfig bullseyeMelody{};
  // Must match ShotStopperNetwork::REQUEST_BODY_CAPACITY (asserted in begin()).
  char requestBody[2048]{};
  WifiScanSnapshot wifiScan{};
};

// Wi-Fi scan snapshots. Network task / httpd only; not BLE.
// AP records come from Arduino's SCAN_DONE cache (getScanInfoByIndex).
static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScan;
static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScanWorking;
static SHOT_STOPPER_PSRAM_BSS PersistedSettings g_networkSettings;

namespace {

NetworkWorkBuf *g_work = nullptr;

void quietIdfWifiDriverWarnings() {
  // Home APs often advertise 802.11r (FT-PSK). STA leaves FT off on purpose:
  // the machine does not roam, and enabling 11r has broken IoT joins on some
  // mesh/UniFi setups. The closed-source driver then WARNs on every matching
  // BSS and falls back to WPA2-PSK. Association still succeeds; Shot Stopper
  // logs join/fail itself. Drop the wifi tag to ERROR so that expected
  // fallback does not flood USB serial. Other IDF tags stay at WARN.
  esp_log_level_set("wifi", ESP_LOG_ERROR);
}

void formatWifiMac(const uint8_t mac[6], char *output, size_t outputCapacity) {
  if (mac == nullptr || output == nullptr || outputCapacity < 18) {
    return;
  }
  snprintf(output, outputCapacity, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void parseShotsPageQuery(httpd_req_t *request, size_t &offset, size_t &limit,
                         ShotLogSort &sort, ShotLogSortDir &dir) {
  offset = 0;
  limit = SHOT_LOG_PAGE_DEFAULT;
  sort = ShotLogSort::Date;
  dir = ShotLogSortDir::Desc;
  if (request == nullptr) {
    return;
  }
  const size_t queryLength = httpd_req_get_url_query_len(request);
  if (queryLength == 0 || queryLength >= 80) {
    return;
  }
  char query[80] = {};
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
    return;
  }
  char value[16] = {};
  if (httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end != value && *end == '\0') {
      offset = static_cast<size_t>(parsed);
    }
  }
  if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end != value && *end == '\0') {
      limit = shotLogClampPageLimit(static_cast<size_t>(parsed));
    }
  }
  memset(value, 0, sizeof(value));
  if (httpd_query_key_value(query, "sort", value, sizeof(value)) == ESP_OK) {
    sort = shotLogSortFromName(value);
  }
  memset(value, 0, sizeof(value));
  if (httpd_query_key_value(query, "dir", value, sizeof(value)) == ESP_OK) {
    dir = shotLogSortDirFromName(value);
  }
}

const char *jsonParseFailureMessage(const char *fallback) {
  if (jsonArenaExhaustedRecently()) {
    return "JSON too large for device buffer";
  }
  return fallback;
}

constexpr const char *AP_SSID = "AdvancedShotStopperAP";
constexpr const char *AP_IP = "192.168.4.1";
constexpr const char *JSON_CONTENT_TYPE = "application/json";
constexpr const char *STATUS_OK = "200 OK";
constexpr const char *STATUS_NO_CONTENT = "204 No Content";
constexpr const char *STATUS_NOT_MODIFIED = "304 Not Modified";
constexpr const char *STATUS_ACCEPTED = "202 Accepted";
constexpr size_t IF_NONE_MATCH_CAPACITY = 80;
constexpr const char *STATUS_BAD_REQUEST = "400 Bad Request";
constexpr const char *STATUS_UNAUTHORIZED = "401 Unauthorized";
constexpr const char *STATUS_FORBIDDEN = "403 Forbidden";
constexpr const char *STATUS_TOO_LARGE = "413 Content Too Large";
constexpr const char *STATUS_SERVER_ERROR = "500 Internal Server Error";
constexpr const char *DEVICE_PASSWORD_HEADER = "X-Device-Password";
// Legacy header name kept so older CLI scripts still authenticate.
constexpr const char *DEVICE_PASSWORD_HEADER_LEGACY = "X-OTA-Token";
constexpr const char *OTA_ALLOW_DOWNGRADE_HEADER = "X-OTA-Allow-Downgrade";
constexpr size_t OTA_STATUS_JSON_CAPACITY = NetworkWorkBuf::kOtaJson;
constexpr const char *STATUS_TOO_MANY = "429 Too Many Requests";
constexpr const char *STATUS_CONFLICT = "409 Conflict";
constexpr const char *STATUS_NOT_FOUND = "404 Not Found";
constexpr const char *STATUS_UNPROCESSABLE = "422 Unprocessable Entity";
constexpr const char *STATUS_UNAVAILABLE = "503 Service Unavailable";
// PersistedSettings is ≤ PERSISTED_SETTINGS_NVS_BUDGET (3072 B).
// processPersistedCommand() keeps one copy on the stack via settingsCopy();
// NVS dual-slot scratch is shared off-stack. 7 168 was too small (canary on
// FACTORY_RESET). 10 240 keeps headroom without the old 12 KiB margin.
// Keep this stack in internal RAM: the task writes NVS (flash cache disabled).
constexpr uint32_t NETWORK_MANAGER_TASK_STACK_SIZE = 10240;
// POST JSON bodies live in NetworkWorkBuf (PSRAM), so the httpd worker no
// longer needs a 2 KiB request-body frame on top of headers and send buffers.
// Stack stays internal: OTA flash writes run on this task.
constexpr uint32_t HTTP_SERVER_TASK_STACK_SIZE = 8192;

const char *scaleDisconnectReasonName(uint8_t reason) {
  // Mirrors ScaleDisconnectReason without coupling the network task to the
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
    case 14: return "SUPERVISION_TIMEOUT";
    case 15: return "CONNECTION_FAILED_TO_ESTABLISH";
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

// Compared in constant time so a wrong secret cannot be recovered one byte at a
// time by measuring how long the rejection takes.
bool secretsMatch(const char *candidate, const char *expected) {
  const size_t candidateLength = strnlen(candidate, WIFI_PASSWORD_CAPACITY);
  const size_t expectedLength = strnlen(expected, WIFI_PASSWORD_CAPACITY);
  uint32_t difference =
      static_cast<uint32_t>(candidateLength ^ expectedLength);
  const size_t span =
      candidateLength < expectedLength ? candidateLength : expectedLength;
  for (size_t index = 0; index < span; ++index) {
    difference |= static_cast<uint32_t>(
        static_cast<uint8_t>(candidate[index]) ^
        static_cast<uint8_t>(expected[index]));
  }
  return difference == 0 && expectedLength > 0;
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
  // Two fixed words cover the broad config patch without heap allocation.
  // Keep the ceiling explicit so callers fail loudly if they outgrow it.
  constexpr size_t kSeenWords = 2;
  if (!cJSON_IsObject(object) || allowed == nullptr ||
      allowedCount > kSeenWords * 64U) {
    return false;
  }
  uint64_t seen[kSeenWords] = {};
  for (cJSON *item = object->child; item != nullptr; item = item->next) {
    if (item->string == nullptr) {
      return false;
    }
    size_t index = 0;
    while (index < allowedCount && strcmp(item->string, allowed[index]) != 0) {
      ++index;
    }
    if (index == allowedCount) {
      return false;
    }
    const size_t word = index / 64U;
    const uint64_t bit = uint64_t{1} << (index % 64U);
    if ((seen[word] & bit) != 0) {
      return false;
    }
    seen[word] |= bit;
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
    case ConfigValidationError::POST_TARE_BASELINE_GRACE:
      return "Post-tare grace must be from 0.5 to 10 s.";
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
    case ConfigValidationError::MIN_BBW_BREW_TIME:
      return "Min BBW brew time must be from 5 to 55 s.";
    case ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION:
      return "Fast guard requires max recovery > target, min BBW brew time < Max BBW "
             "time, and min BBW brew time ≥ BBW protection.";
    case ConfigValidationError::MIN_RECOVERY_WEIGHT:
      return "Min recovery must be from 10 to 200 g.";
    case ConfigValidationError::MAX_BBW_BREW_TIME:
      return "Max BBW brew time must be from 5 to 55 s.";
    case ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION:
      return "Slow guard requires min recovery < target, max BBW brew time < Max BBW "
             "time, max BBW brew time ≥ BBW protection, and max BBW brew time > min BBW brew time "
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
      return "Scale preference must be first, prefer, or only.";
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
    case ConfigValidationError::NO_SCALE_BBW_MODE:
      return "No-scale BBW mode must be off, warn_once, or require_scale.";
    case ConfigValidationError::LAST_SHOT_COOLDOWN:
      return "Last shot cooldown must be from 5 to 240 min.";
    case ConfigValidationError::DRIP_DELAY:
      return "Drip delay must be from 0 to 10 s.";
    case ConfigValidationError::RING_RETAIN_LOG_LEVEL:
      return "Ring log level must be none, critical, error, warning, info, or "
             "debug.";
    case ConfigValidationError::PADDLE_MODE:
      return "Paddle mode must be auto, natural or original.";
    case ConfigValidationError::CUP_PRESENT_WEIGHT:
      return "Cup-present threshold must be from 0.1 to 50 g.";
    case ConfigValidationError::CUP_REMOVED_WEIGHT:
      return "Cup-removed threshold must be from -50 to -0.1 g.";
    case ConfigValidationError::STOP_PULSE:
      return "Auto-stop pulse must be from 50 to 1000 ms.";
    case ConfigValidationError::MAX_SINGLE_PRESS:
      return "Single-press limit must be from 100 to 5000 ms.";
    case ConfigValidationError::REED_CONFIRM_TIMEOUT:
      return "Reed confirm timeout must be from 0.2 to 5 s.";
    case ConfigValidationError::SHOT_REACT_TIMEOUT:
      return "Shot reaction timeout must be 0 (compiled default) or from 3 to 30 s.";
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

bool jsonNoScaleBbwMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "off") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::OFF);
    return true;
  }
  if (strcmp(item->valuestring, "warn_once") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
    return true;
  }
  if (strcmp(item->valuestring, "require_scale") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
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

bool jsonStopPulseMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "stopPulseMs", ms) || ms < 50 || ms > 1000) {
    return false;
  }
  setRuntimeStopPulseMs(config, ms);
  return true;
}

bool jsonMaxSinglePressMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "maxSinglePressMs", ms) || ms < 100 || ms > 5000) {
    return false;
  }
  setRuntimeMaxSinglePressMs(config, ms);
  return true;
}

bool jsonReedConfirmTimeoutMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "reedConfirmTimeoutMs", ms) || ms < 200 ||
      ms > 5000) {
    return false;
  }
  setRuntimeReedConfirmTimeoutMs(config, ms);
  return true;
}

bool jsonAssumeIdleWhenScaleConnects(cJSON *object, RuntimeConfig &config) {
  return jsonBoolean(object, "assumeIdleWhenScaleConnects",
                     config.assumeIdleWhenScaleConnects);
}

bool jsonShotReactTimeoutS(cJSON *object, RuntimeConfig &config) {
  uint8_t seconds = 0;
  if (!jsonUint8(object, "shotReactTimeoutS", seconds)) {
    return false;
  }
  if (seconds != 0 && (seconds < MIN_SHOT_REACT_TIMEOUT_S ||
                       seconds > MAX_SHOT_REACT_TIMEOUT_S)) {
    return false;
  }
  config.shotReactTimeoutS = seconds;
  return true;
}

bool jsonMomentaryStartEdge(cJSON *object, RuntimeConfig &config) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "momentaryStartEdge");
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseMomentaryStartEdge(item->valuestring, config.momentaryStartOnPress);
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

// lwIP on ESP32-S3 cannot DMA a PSRAM or flash pointer in one tcp_write.
// Stream through a small internal bounce buffer; no full-body staging in DRAM.
// Single httpd server task (HTTPD_DEFAULT_CONFIG). Do not raise workers without
// giving this bounce its own lock or per-handler stack storage.
constexpr size_t HTTP_DRAM_BOUNCE_BYTES = 512;

static uint8_t g_httpSendBounce[HTTP_DRAM_BOUNCE_BYTES];

esp_err_t sendCopiedChunk(httpd_req_t *request, const void *data,
                          size_t length) {
  const auto *src = static_cast<const uint8_t *>(data);
  while (length > 0) {
    const size_t n =
        length < HTTP_DRAM_BOUNCE_BYTES ? length : HTTP_DRAM_BOUNCE_BYTES;
    memcpy(g_httpSendBounce, src, n);
    if (httpd_resp_send_chunk(request,
                              reinterpret_cast<const char *>(g_httpSendBounce),
                              n) != ESP_OK) {
      return ESP_FAIL;
    }
    src += n;
    length -= n;
  }
  return ESP_OK;
}

esp_err_t sendCopiedBody(httpd_req_t *request, const void *data,
                         size_t length) {
  if (length == 0) {
    return httpd_resp_send(request, nullptr, 0);
  }
  if (sendCopiedChunk(request, data, length) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t sendJsonStringChunk(httpd_req_t *request, const char *value) {
  if (value == nullptr) {
    value = "";
  }
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
          sendCopiedChunk(request, segment,
                          static_cast<size_t>(cursor - segment)) != ESP_OK) {
        return ESP_FAIL;
      }
      if (sendCopiedChunk(request, escape, strlen(escape)) != ESP_OK) {
        return ESP_FAIL;
      }
      segment = cursor + 1;
    }
    ++cursor;
  }
  if (cursor > segment &&
      sendCopiedChunk(request, segment,
                      static_cast<size_t>(cursor - segment)) != ESP_OK) {
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

bool jsonScratchAppend(char *buf, size_t cap, size_t *used, const char *fmt,
                       ...) {
  if (buf == nullptr || used == nullptr || *used >= cap) {
    return false;
  }
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf + *used, cap - *used, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap - *used) {
    return false;
  }
  *used += static_cast<size_t>(n);
  return true;
}

bool formatTaskProfilerObject(char *buf, size_t cap, size_t *used,
                              const TaskProfilerSnapshot &tasks) {
  if (!jsonScratchAppend(
          buf, cap, used,
          "{\"state\":\"%s\",\"stopReason\":\"%s\","
          "\"elapsedMs\":%lu,\"remainingMs\":%lu,\"sampleCount\":%lu,"
          "\"currentTotalCpuPct\":%.1f,\"averageTotalCpuPct\":%.1f,"
          "\"unreportedCurrentCpuPct\":%.1f,\"unreportedAverageCpuPct\":%.1f,"
          "\"truncated\":%s,\"lastCaptureUs\":%lu,\"maxCaptureUs\":%lu,"
          "\"rows\":[",
          taskProfilerStateName(tasks.state),
          taskProfilerStopReasonName(tasks.stopReason),
          static_cast<unsigned long>(tasks.elapsedMs),
          static_cast<unsigned long>(tasks.remainingMs),
          static_cast<unsigned long>(tasks.sampleCount),
          static_cast<double>(tasks.currentTotalCpuPct),
          static_cast<double>(tasks.averageTotalCpuPct),
          static_cast<double>(tasks.unreportedCurrentCpuPct),
          static_cast<double>(tasks.unreportedAverageCpuPct),
          tasks.truncated ? "true" : "false",
          static_cast<unsigned long>(tasks.lastCaptureUs),
          static_cast<unsigned long>(tasks.maxCaptureUs))) {
    return false;
  }
  for (uint8_t i = 0; i < tasks.rowCount; ++i) {
    char safeName[TASK_PROFILER_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(tasks.rows[i].name, safeName, sizeof(safeName));
    if (!jsonScratchAppend(
            buf, cap, used,
            "%s{\"name\":\"%s\",\"core\":%d,\"currentCpuPct\":%.1f,"
            "\"averageCpuPct\":%.1f,\"stackMinWords\":%lu}",
            i == 0 ? "" : ",", safeName, static_cast<int>(tasks.rows[i].core),
            static_cast<double>(tasks.rows[i].currentCpuPct),
            static_cast<double>(tasks.rows[i].averageCpuPct),
            static_cast<unsigned long>(tasks.rows[i].stackMinWords))) {
      return false;
    }
  }
  return jsonScratchAppend(buf, cap, used, "]}");
}

bool statusJsonAppendTaskProfiler(size_t *used,
                                  const TaskProfilerSnapshot &tasks) {
  if (!statusJsonAppend(used, ",\"tasks\":")) {
    return false;
  }
  return formatTaskProfilerObject(g_work->statusJson, NetworkWorkBuf::kStatusJson,
                                  used, tasks);
}

void buildSlimPresetsJson(const ShotPresetBank &presets) {
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
                   static_cast<unsigned>(presets.activeId));
  if (n > 0) {
    used = static_cast<size_t>(n);
  }
  for (uint8_t i = 0; i < presets.count && i < MAX_SHOT_PRESETS; ++i) {
    const ShotPreset &p = presets.presets[i];
    char safeName[SHOT_PRESET_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(p.name, safeName, sizeof(safeName));
    n = snprintf(
        buf + used, cap - used,
        "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s,\"brewByWeight\":%s,"
        "\"goalWeightG\":%u,\"minBbwBrewTimeMs\":%lu,\"maxRecoveryWeightG\":%.1f}",
        i == 0 ? "" : ",", static_cast<unsigned>(p.id), safeName,
        p.isFactory ? "true" : "false", p.brewByWeight ? "true" : "false",
        static_cast<unsigned>(p.goalWeightG),
        static_cast<unsigned long>(p.minBbwBrewTimeMs),
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

void buildScaleHistoryJson(const ScaleHistoryEntry *entries) {
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
    const ScaleHistoryEntry &entry = entries[i];
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

ShotStopperNetwork::ShotStopperNetwork() : settings_(g_networkSettings) {}

bool ShotStopperNetwork::begin(const PersistedSettings &settings,
                               const NetworkBridgeCallbacks &callbacks) {
  if (instance_ != nullptr || callbacks.copyControlStatus == nullptr ||
      callbacks.copyControlGate == nullptr ||
      callbacks.refreshControlStatus == nullptr ||
      callbacks.enqueueWebCommand == nullptr ||
      callbacks.copyDebugEvents == nullptr ||
      callbacks.reportTaskWatchdogFault == nullptr ||
      callbacks.requestSafeRestart == nullptr) {
    return false;
  }
  quietIdfWifiDriverWarnings();
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
      allocExternal(sizeof(NetworkWorkBuf)));
  if (workBuf_ == nullptr) {
    vSemaphoreDelete(statusResponseMux_);
    statusResponseMux_ = nullptr;
    vQueueDelete(acceptedCommandQueue_);
    acceptedCommandQueue_ = nullptr;
    noteWorkBufExternal(false);
    return false;
  }
  new (workBuf_) NetworkWorkBuf{};
  noteWorkBufExternal(true);
  auto destroyWorkBuf = [&]() {
    workBuf_->~NetworkWorkBuf();
    heapCapsFree(workBuf_);
    workBuf_ = nullptr;
    noteWorkBufExternal(false);
  };
  memset(&g_wifiScan, 0, sizeof(g_wifiScan));
  memset(&g_wifiScanWorking, 0, sizeof(g_wifiScanWorking));
  static_assert(REQUEST_BODY_CAPACITY == 2048,
                "NetworkWorkBuf::requestBody must match REQUEST_BODY_CAPACITY");
  g_work = workBuf_;
  instance_ = this;
  initJsonArenaHooks();
  ntpConfigRevision_ = settings_.runtime.revision;
  g_wallClock.reset();
  ensureRfCoexBt();
  // Pin beside the Wi-Fi/LwIP stacks on PRO_CPU (core 0).
  // Stack must stay in internal RAM: this task calls NVS/Preferences (flash
  // write disables the cache, which makes a PSRAM stack inaccessible).
  if (xTaskCreatePinnedToCore(taskEntry, "network_manager",
                              NETWORK_MANAGER_TASK_STACK_SIZE, this,
                              tskIDLE_PRIORITY + 1, &taskHandle_,
                              0) != pdPASS) {
    instance_ = nullptr;
    g_work = nullptr;
    destroyWorkBuf();
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
  return xSemaphoreTake(statusResponseMux_, pdMS_TO_TICKS(2500)) == pdTRUE;
}

void ShotStopperNetwork::unlockWorkBuf() {
  xSemaphoreGive(statusResponseMux_);
}

void ShotStopperNetwork::unlockJsonBody() {
  if (workBuf_ != nullptr) {
    memset(workBuf_->requestBody, 0, sizeof(workBuf_->requestBody));
  }
  unlockWorkBuf();
}

esp_err_t ShotStopperNetwork::lockJsonBody(httpd_req_t *request,
                                           const char *invalidMessage) {
  if (!lockWorkBuf()) {
    return workBufBusy(request);
  }
  if (!readJsonBody(request, workBuf_->requestBody)) {
    unlockJsonBody();
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST",
                     invalidMessage);
  }
  return ESP_OK;
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

StaJoinHints ShotStopperNetwork::staJoinHints() {
  StaJoinHints hints;
  portENTER_CRITICAL(&dataMux_);
  hints.staConfigured = settings_.staConfigured;
  hints.staOpen = settings_.staOpen;
  hints.staConfigState = settings_.staConfigState;
  memcpy(hints.staSsid, settings_.staSsid, sizeof(hints.staSsid));
  portEXIT_CRITICAL(&dataMux_);
  return hints;
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
  if (callbacks_.copyBullseyeConfig != nullptr) {
    callbacks_.copyBullseyeConfig(&settings.bullseyeMelody);
  }
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
    copyCString(safeName, sizeof(safeName), name);
  }
  portENTER_CRITICAL(&dataMux_);
  copyCString(settings_.preferredScaleMac, sizeof(settings_.preferredScaleMac),
              mac);
  copyCString(settings_.preferredScaleName, sizeof(settings_.preferredScaleName),
              safeName);
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

void ShotStopperNetwork::syncLiveBullseye(
    const BullseyeMelodyConfig &config) {
  portENTER_CRITICAL(&dataMux_);
  settings_.bullseyeMelody = config;
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

  if (restartPending_) {
    // NVS is already committed. Do not touch the radio on the way to
    // ESP.restart(): Arduino WiFi.mode(WIFI_OFF) deinits the driver
    // (esp_wifi_deinit) and tears down BT coexistence while bleTask is
    // still sending VHCI packets. The S3 restart path already resets
    // Wi-Fi/BT MAC in esp_system_reset_modules_on_exit().
    if (controlAllowsNetworkMutation() &&
        static_cast<uint32_t>(now - restartRequestedAtMs_) >=
            RESTART_DELAY_MS) {
      restartPending_ = false;
      callbacks_.requestSafeRestart();
    }
    return;
  }

  if (!startupComplete_ &&
      static_cast<int32_t>(now - networkRetryAtMs_) >= 0) {
    if (startNetwork()) {
      startupFailures_ = 0;
      networkRetryAtMs_ = 0;
      portENTER_CRITICAL(&dataMux_);
      status_.startupFailures = 0;
      portEXIT_CRITICAL(&dataMux_);
      // startStation/beginStationConnect delay and wait for STA_START, so
      // `now` is stale. serviceStaState would treat the connect timer as
      // expired (uint32 wrap) and nest SoftAP+httpd on this stack.
      return;
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
  // Confirmation and rollback of a pending image cannot wait for the network
  // to come up: that is the very failure they exist to recover from.
  serviceOtaRollback(now);
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
  applyWifiPowerSave();
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
  serviceSoftApIdle(now);
  refreshExtendedStatus(now);

  // Gate check is a 16-byte ControlGateSnapshot, not the 4 KiB status
  // snapshot. Do not reintroduce a stack copy here: httpd_stop() / LwIP
  // still share this frame.
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

}

bool ShotStopperNetwork::controlAllowsNetworkMutation() {
  return controlAllowsConfiguration(controlGate());
}

ControlGateSnapshot ShotStopperNetwork::controlGate() const {
  ControlGateSnapshot gate;
  if (callbacks_.copyControlGate != nullptr) {
    callbacks_.copyControlGate(gate);
  }
  return gate;
}

bool ShotStopperNetwork::lockWorkBufForStatus() {
  if (callbacks_.refreshControlStatus != nullptr) {
    callbacks_.refreshControlStatus();
  }
  return lockWorkBuf();
}

void ShotStopperNetwork::loadControlStatus(ControlStatusSnapshot &control) {
  callbacks_.copyControlStatus(control);
}

bool ShotStopperNetwork::startNetwork() {
  const PersistedSettings settings = settingsCopy();
  const uint32_t now = millis();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  portENTER_CRITICAL(&dataMux_);
  status_.wifiConfigured = settings.staConfigured;
  copyCString(status_.apIp, sizeof(status_.apIp), AP_IP);
  portEXIT_CRITICAL(&dataMux_);
  publishConfiguredAddressStatus();
  if (settings.staConfigured) {
    startStation(settings, now);
    startupComplete_ = true;
    return true;
  }
  if (apAutoRaiseExhausted_ || apStartHeld_) {
    lifecycleLog(apStartHeld_ ? "SoftAP raise held at boot"
                              : "SoftAP auto-raise exhausted at boot");
    startupComplete_ = false;
    return false;
  }
  startupComplete_ = ensureAccessPoint(now);
  return startupComplete_;
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
    copyCString(staSsid, sizeof(staSsid), settings.staSsid);
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
  status_.staWifiSleep = settings.staWifiSleep;
  status_.staIpMode = settings.staIpMode;
  status_.staConfigState = settings.staConfigState;
  memset(status_.staSsid, 0, sizeof(status_.staSsid));
  copyCString(status_.staSsid, sizeof(status_.staSsid), staSsid);
  copyCString(status_.configuredIp, sizeof(status_.configuredIp), configuredIp);
  copyCString(status_.configuredNetmask, sizeof(status_.configuredNetmask),
              configuredNetmask);
  copyCString(status_.configuredGateway, sizeof(status_.configuredGateway),
              configuredGateway);
  copyCString(status_.configuredDns1, sizeof(status_.configuredDns1),
              configuredDns1);
  copyCString(status_.configuredDns2, sizeof(status_.configuredDns2),
              configuredDns2);
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
    // Do not startStation/ensureAccessPoint here: this frame already holds
    // settingsCopy() plus service()/serviceStaState. httpd_start on that nest
    // overflows the 10 KiB network task (pthread TLS LoadProhibited).
    startupComplete_ = false;
    return true;
  }
  if (staEverConnected_) {
    stopHttpServer();
    WiFi.softAPdisconnect(true);
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.disconnect(false, false);
    }
    portENTER_CRITICAL(&dataMux_);
    status_.networkActive = false;
    status_.apActive = false;
    status_.apClients = 0;
    status_.staState = StaState::NOT_CONFIGURED;
    status_.staIp[0] = '\0';
    clearStaLinkMetrics();
    portEXIT_CRITICAL(&dataMux_);
    lifecycleLog(
        "WiFi STA cleared after prior connect; SoftAP suppressed (AP_START or reboot)");
    return true;
  }
  startupComplete_ = false;
  return true;
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
  }
  // DHCP: do not WiFi.config(INADDR_NONE,...). That path calls STA.begin()
  // with tryConnect=true and can disconnect while STA_START is in flight.
}

void ShotStopperNetwork::clearStaLinkMetrics() {
  status_.staLinkMetricsValid = false;
  status_.staRssi = 0;
  status_.staSignalQualityPct = 0;
  status_.staBssid[0] = '\0';
}

bool ShotStopperNetwork::brewRfActive() const {
  const ControlGateSnapshot control = controlGate();
  return control.activeCycle || control.relayClosed;
}

void ShotStopperNetwork::syncScaleLinkRf(bool connectingOrUp) {
  scaleConnectingOrUp_.store(connectingOrUp, std::memory_order_relaxed);
}

void ShotStopperNetwork::syncScaleConnectingRf(bool connecting) {
  scaleConnecting_.store(connecting, std::memory_order_relaxed);
}

void ShotStopperNetwork::syncScaleHuntRf(bool huntActive) {
  scaleHuntRfActive_.store(huntActive, std::memory_order_relaxed);
}

void ShotStopperNetwork::applyWifiPowerSave() {
  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_OFF) {
    lastAppliedWifiPsValid_ = false;
    return;
  }
  bool sleepAllowed = false;
  bool apActive = false;
  bool staAssociated = false;
  portENTER_CRITICAL(&dataMux_);
  sleepAllowed = settings_.staWifiSleep;
  apActive = status_.apActive;
  staAssociated = status_.staState == StaState::CONNECTED;
  portEXIT_CRITICAL(&dataMux_);
  const bool otaBusy = ShotStopperOta::instance().busy();
  const WifiPowerSaveMode desired = desiredWifiPowerSave(
      sleepAllowed, apActive, staAssociated, otaBusy);
  const wifi_ps_type_t desiredPs = desired == WifiPowerSaveMode::MIN_MODEM
                                       ? WIFI_PS_MIN_MODEM
                                       : WIFI_PS_NONE;
  wifi_ps_type_t current = WIFI_PS_NONE;
  if (esp_wifi_get_ps(&current) == ESP_OK && lastAppliedWifiPsValid_ &&
      current == desiredPs && lastAppliedWifiPs_ == desiredPs) {
    return;
  }
  if (!WiFi.setSleep(desiredPs)) {
    lastAppliedWifiPsValid_ = false;
    lifecycleLog("WiFi power save set failed");
    return;
  }
  wifi_ps_type_t verified = WIFI_PS_NONE;
  if (esp_wifi_get_ps(&verified) != ESP_OK || verified != desiredPs) {
    lastAppliedWifiPsValid_ = false;
    lifecycleLog("WiFi power save verify mismatch");
    return;
  }
  lastAppliedWifiPs_ = desiredPs;
  lastAppliedWifiPsValid_ = true;
}

bool ShotStopperNetwork::beginStationConnect(const PersistedSettings &settings,
                                             uint32_t now) {
  if (brewRfActive()) {
    lifecycleLog("WiFi STA associate deferred; brew RF active");
    return false;
  }
  if (scaleConnecting_.load(std::memory_order_relaxed)) {
    lifecycleLog("WiFi STA associate deferred; scale connecting");
    return false;
  }
  const bool apActive = snapshot().apActive;
  if (apActive) {
    WiFi.mode(WIFI_AP_STA);
  } else {
    WiFi.mode(WIFI_STA);
  }
  // Drop any UI scan so association owns the radio.
  abortWifiScan(now, false);
  // Drop a stale STA association so 10 s retries do not accumulate IDF/DHCP
  // state. wifioff=false, eraseap=false: driver and SoftAP stay up (BLE coex).
  WiFi.disconnect(false, false);
  portENTER_CRITICAL(&dataMux_);
  status_.staState = StaState::CONNECTING;
  status_.staIp[0] = '\0';
  clearStaLinkMetrics();
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
  // Do NOT pre-scan and lock a BSSID: an incomplete scan (BLE coexistence)
  // often sees only the weak BSS and then forces sticky association to it.
  // Let the IDF connect path scan every channel and sort by RSSI.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  if (!feedCurrentTaskWatchdog()) {
    callbacks_.reportTaskWatchdogFault();
  }

  staConnectStartedAtMs_ = millis();
  staReconnectAttemptAtMs_ = staConnectStartedAtMs_;
  lifecycleLog("WiFi STA associating with all-channel RSSI sort");
  // begin(false): enable STA and wait for STA_START without connect().
  // WiFi.begin() uses begin(true) then connect(ssid), and that extra connect()
  // can disconnect while the start handler runs (ESP_ERR_WIFI_STOP_STATE 12308).
  if (!WiFi.STA.begin(false)) {
    lifecycleLog("WiFi STA enable failed");
    return false;
  }
  applyWifiPowerSave();
  applyStationAddressConfig(settings);
  (void)WiFi.STA.connect(settings.staSsid,
                         settings.staOpen ? nullptr : settings.staPassword);
  return true;
}

void ShotStopperNetwork::startStation(const PersistedSettings &settings,
                                      uint32_t now) {
  stopHttpServer();
  abortWifiScan(now, false);
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.softAPdisconnect(true);
  }
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
  clearStaLinkMetrics();
  status_.windowRemainingMs = 0;
  status_.confirmRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  publishConfiguredAddressStatus();
  if (!beginStationConnect(settings, now)) {
    portENTER_CRITICAL(&dataMux_);
    if (status_.staState == StaState::CONNECTING) {
      status_.staState = StaState::DISCONNECTED;
    }
    portEXIT_CRITICAL(&dataMux_);
  }
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
  clearSoftApIdleState();
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
  lifecycleLog(stopHttp ? "WiFi SoftAP stopped; HTTP stopped"
                        : "WiFi SoftAP stopped; HTTP kept");
  applyWifiPowerSave();
}

void ShotStopperNetwork::stopSoftApKeepStation() {
  stopSoftAp(true);
}

void ShotStopperNetwork::stopSoftApLeaveHttp() {
  stopSoftAp(false);
}

void ShotStopperNetwork::clearSoftApIdleState() {
  apIdleDeadlineMs_ = 0;
  lastApClients_ = 0;
}

void ShotStopperNetwork::armSoftApIdleDeadline(uint32_t now) {
  if (apKeepRequested_) {
    clearSoftApIdleState();
    return;
  }
  apIdleDeadlineMs_ = now + SOFTAP_IDLE_TIMEOUT_MS;
  lastApClients_ = 0;
}

void ShotStopperNetwork::serviceSoftApIdle(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (!status.apActive || apKeepRequested_) {
    if (!status.apActive) {
      clearSoftApIdleState();
    } else {
      lastApClients_ = status.apClients;
      apIdleDeadlineMs_ = 0;
    }
    return;
  }

  const uint8_t clients = status.apClients;
  if (clients > 0) {
    apIdleDeadlineMs_ = 0;
    lastApClients_ = clients;
    return;
  }

  // Zero SoftAP stations: arm/re-arm on raise or when the last client leaves.
  if (lastApClients_ > 0 || apIdleDeadlineMs_ == 0) {
    armSoftApIdleDeadline(now);
  }
  lastApClients_ = 0;

  if (apIdleDeadlineMs_ == 0 ||
      static_cast<int32_t>(now - apIdleDeadlineMs_) < 0) {
    return;
  }

  apAutoRaiseExhausted_ = true;
  clearSoftApIdleState();
  lifecycleLog("WiFi SoftAP idle timeout; SoftAP down for rest of boot");
  const bool staUp = status.staState == StaState::CONNECTED &&
                     WiFi.status() == WL_CONNECTED;
  if (staUp) {
    stopSoftApLeaveHttp();
  } else {
    stopSoftAp(true);
  }
}

bool ShotStopperNetwork::wifiScanInProgress() {
  bool busy = false;
  portENTER_CRITICAL(&dataMux_);
  busy = scanRequested_ || g_wifiScan.state == WifiScanState::QUEUED ||
         g_wifiScan.state == WifiScanState::RUNNING;
  portEXIT_CRITICAL(&dataMux_);
  return busy;
}

void ShotStopperNetwork::abortWifiScan(uint32_t now, bool logTimeout) {
  bool wasRunning = false;
  bool wasQueuedOrRequested = false;
  portENTER_CRITICAL(&dataMux_);
  wasRunning = g_wifiScan.state == WifiScanState::RUNNING;
  wasQueuedOrRequested =
      scanRequested_ || g_wifiScan.state == WifiScanState::QUEUED || wasRunning;
  if (wasQueuedOrRequested) {
    scanRequested_ = false;
    g_wifiScan.state = logTimeout ? WifiScanState::FAILED : WifiScanState::CANCELED;
    g_wifiScan.updatedAtMs = now;
    g_wifiScan.count = 0;
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
  if (!force && (apAutoRaiseExhausted_ || apStartHeld_)) {
    lifecycleLog(apStartHeld_ ? "SoftAP raise held"
                              : "SoftAP auto-raise exhausted");
    return false;
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
    if (WiFi.getMode() != WIFI_OFF) {
      WiFi.disconnect(false, false);
    }
    WiFi.mode(WIFI_AP);
  } else {
    // Preserve any in-flight STA association attempt while SoftAP is raised.
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_AP_STA);
  }
  const IPAddress ip(192, 168, 4, 1);
  const bool configured =
      WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool apReady = configured && WiFi.softAP(AP_SSID, settings.devicePassword);
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
    clearStaLinkMetrics();
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
    // Keep the driver initialized: WIFI_OFF deinits and breaks BLE coexistence.
    portENTER_CRITICAL(&dataMux_);
    status_.apActive = false;
    status_.networkActive = false;
    portEXIT_CRITICAL(&dataMux_);
    clearSoftApIdleState();
    applyWifiPowerSave();
    return false;
  }
  armSoftApIdleDeadline(now);
  applyWifiPowerSave();
  return true;
}

void ShotStopperNetwork::stopNetwork() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  stopHttpServer();
  WiFi.softAPdisconnect(true);
  if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
  }
  // Do not WiFi.mode(WIFI_OFF): Arduino 3.x deinits the driver there
  // (esp_wifi_deinit) and breaks BLE VHCI coexistence.
  portENTER_CRITICAL(&dataMux_);
  scanRequested_ = false;
  g_wifiScan = WifiScanSnapshot{};
  status_.networkActive = false;
  status_.apActive = false;
  status_.apClients = 0;
  status_.staState = status_.wifiConfigured ? StaState::DISCONNECTED
                                            : StaState::NOT_CONFIGURED;
  status_.staIp[0] = '\0';
  clearStaLinkMetrics();
  status_.windowRemainingMs = 0;
  portEXIT_CRITICAL(&dataMux_);
  clearSoftApIdleState();
  log(DebugCategory::NETWORK, DebugCode::AP_STOPPED);
}

void ShotStopperNetwork::serviceStaState(uint32_t now) {
  NetworkStatusSnapshot status = snapshot();
  if (!status.wifiConfigured) {
    return;
  }

  const bool brewRf = brewRfActive();
  const bool scaleConnecting =
      scaleConnecting_.load(std::memory_order_relaxed);
  // Abort an in-flight scan/associate only. A STA that already has an IP
  // must promote to CONNECTED: associated traffic can coexist under PREFER_BT.
  if ((brewRf || scaleConnecting) && status.staState == StaState::CONNECTING &&
      WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false, false);
    portENTER_CRITICAL(&dataMux_);
    status_.staState = StaState::DISCONNECTED;
    portEXIT_CRITICAL(&dataMux_);
    lifecycleLog(brewRf ? "WiFi STA associate aborted; brew RF active"
                        : "WiFi STA associate aborted; scale connecting");
    applyWifiPowerSave();
    return;
  }

  if (status.staState == StaState::CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::DISCONNECTED;
      status_.networkActive = false;
      status_.staIp[0] = '\0';
      clearStaLinkMetrics();
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
      applyWifiPowerSave();
      if (!staReconnectHeld_ && !wifiScanInProgress() && !brewRf &&
          !scaleConnecting) {
        beginStationConnect(settingsCopy(), now);
      } else if (staReconnectHeld_) {
        lifecycleLog("STA reconnect held; skipping retry");
      } else if (brewRf) {
        lifecycleLog("STA reconnect deferred; brew RF active");
      } else if (scaleConnecting) {
        lifecycleLog("STA reconnect deferred; scale connecting");
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
      char bssidText[18] = {};
      const uint8_t *bssid = WiFi.BSSID();
      if (bssid != nullptr) {
        formatWifiMac(bssid, bssidText, sizeof(bssidText));
      }
      portENTER_CRITICAL(&dataMux_);
      status_.staState = StaState::CONNECTED;
      status_.networkActive = httpReady;
      copyCString(status_.staIp, sizeof(status_.staIp), address);
      status_.staLinkMetricsValid = true;
      status_.staRssi = clampWifiRssi(rssi);
      status_.staSignalQualityPct = wifiRssiToSignalQualityPct(rssi);
      status_.windowRemainingMs = 0;
      copyCString(status_.staBssid, sizeof(status_.staBssid), bssidText);
      portEXIT_CRITICAL(&dataMux_);
      log(DebugCategory::NETWORK, DebugCode::STA_CONNECTED);
      lifecycleLogf("WiFi STA connected; IP: %s bssid=%s rssi=%ld http=%s",
                    address, bssidText[0] != '\0' ? bssidText : "-",
                    static_cast<long>(rssi),
                    httpReady ? "up" : (httpStartHeld_ ? "held" : "down"));
    }
    staNtpEligibleAtMs_ = now + NTP_STA_SETTLE_MS;
    ntpRearmPending_ = true;
    if (settingsCopy().staConfigState ==
        static_cast<uint8_t>(StaConfigState::PENDING)) {
      armPendingConfirmWindow(now);
    }
    applyWifiPowerSave();
    return;
  }

  if (status.staState == StaState::CONNECTING &&
      static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
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
    if (apStartHeld_ || apAutoRaiseExhausted_) {
      lifecycleLog(apStartHeld_ ? "SoftAP raise held after STA fail"
                                : "SoftAP auto-raise exhausted after STA fail");
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
  // scan delayed the reconnect begin). Never after a prior CONNECTED session,
  // AP_STOP hold, or SoftAP idle exhaustion. Also skip while brew RF is active
  // so SoftAP raise cannot steal airtime.
  if (!staEverConnected_ && !status.apActive && !brewRf && !scaleConnecting &&
      !apStartHeld_ && !apAutoRaiseExhausted_ &&
      (status.staState == StaState::DISCONNECTED ||
       status.staState == StaState::FAILED) &&
      static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
          STA_CONNECT_TIMEOUT_MS &&
      !wifiScanInProgress()) {
    if (!ensureAccessPoint(now)) {
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
        static_cast<uint32_t>(millis() - staConnectStartedAtMs_) >=
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
      !staReconnectHeld_ && !wifiScanInProgress() && !brewRf &&
      !scaleConnecting &&
      static_cast<uint32_t>(now - staReconnectAttemptAtMs_) >=
          STA_RECONNECT_INTERVAL_MS) {
    beginStationConnect(settingsCopy(), now);
  }
}

void ShotStopperNetwork::serviceWifiScan(uint32_t now) {
  const ControlGateSnapshot control = controlGate();
  const bool matchingMaintenanceLease =
      scanMaintenanceLeaseId_ != 0 && control.maintenanceLeaseActive &&
      control.maintenanceLeaseId == scanMaintenanceLeaseId_ &&
      !control.activeCycle && !control.relayClosed &&
      !control.physicalActivatorOn;
  const bool safe = controlAllowsConfiguration(control) ||
                    matchingMaintenanceLease;

  bool requested = false;
  WifiScanState state;
  portENTER_CRITICAL(&dataMux_);
  requested = scanRequested_;
  state = g_wifiScan.state;
  portEXIT_CRITICAL(&dataMux_);

  if (scaleHuntRfActive_.load(std::memory_order_relaxed) ||
      scaleConnecting_.load(std::memory_order_relaxed)) {
    const bool canceled = requested || state == WifiScanState::RUNNING ||
                          state == WifiScanState::QUEUED;
    if (canceled) {
      abortWifiScan(now, false);
    }
    return;
  }

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
      g_wifiScan.state = WifiScanState::RUNNING;
      g_wifiScan.updatedAtMs = now;
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
  scanStartedAtMs = g_wifiScan.updatedAtMs;
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
  WifiScanSnapshot &completed = g_wifiScanWorking;
  completed = WifiScanSnapshot{};
  completed.updatedAtMs = now;
  if (resultCount < 0) {
    completed.state = WifiScanState::FAILED;
    WiFi.scanDelete();
    portENTER_CRITICAL(&dataMux_);
    g_wifiScan = completed;
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
  // Arduino's SCAN_DONE handler already copied the IDF AP list into
  // WiFiScanClass::_scanResult. A second IDF fetch returns 0 APs.
  for (int16_t index = 0;
       index < resultCount && completed.count < MAX_WIFI_SCAN_RESULTS;
       ++index) {
    const wifi_ap_record_t *apPtr = static_cast<const wifi_ap_record_t *>(
        WiFi.getScanInfoByIndex(index));
    if (apPtr == nullptr) {
      continue;
    }
    const wifi_ap_record_t &ap = *apPtr;
    const size_t length =
        strnlen(reinterpret_cast<const char *>(ap.ssid), sizeof(ap.ssid));
    if (length == 0 || length >= WIFI_SSID_CAPACITY) {
      continue;
    }
    const int32_t rssi = ap.rssi;
    size_t duplicate = completed.count;
    for (size_t item = 0; item < completed.count; ++item) {
      if (strcmp(completed.networks[item].ssid,
                 reinterpret_cast<const char *>(ap.ssid)) == 0) {
        duplicate = item;
        break;
      }
    }
    if (duplicate < completed.count) {
      if (rssi > completed.networks[duplicate].rssi) {
        completed.networks[duplicate].rssi = rssi;
        completed.networks[duplicate].channel = ap.primary;
        completed.networks[duplicate].open = ap.authmode == WIFI_AUTH_OPEN;
      }
      continue;
    }
    WifiScanNetwork &network = completed.networks[completed.count++];
    memcpy(network.ssid, ap.ssid, length);
    network.ssid[length] = '\0';
    network.rssi = rssi;
    network.channel = ap.primary;
    network.open = ap.authmode == WIFI_AUTH_OPEN;
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
  g_wifiScan = completed;
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

  // Maintenance commands used to keep a ControlStatusSnapshot in this
  // frame. Gate fields live in ControlGateSnapshot so WEBUI_RESTART does not
  // carry that object into httpd_stop() / LwIP.
  processAcceptedMaintenanceCommand(now);
}

void ShotStopperNetwork::processAcceptedMaintenanceCommand(uint32_t now) {
  const ControlGateSnapshot control = controlGate();
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
        control.physicalActivatorOn || publicationTimedOut) {
      (void)enqueueMaintenanceCompletion(
          acceptedCommand_, false, CommandResultState::CANCELED);
      acceptedCommandPending_ = false;
      acceptedCommand_ = WebCommand{};
    }
    return;
  }
  if (control.activeCycle || control.relayClosed ||
      control.physicalActivatorOn) {
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
                           command.type == WebCommandType::CHANGE_DEVICE_PASSWORD ||
                           command.type == WebCommandType::RESET_DEVICE_PASSWORD ||
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
    g_wifiScan = WifiScanSnapshot{};
    g_wifiScan.state = WifiScanState::QUEUED;
    g_wifiScan.updatedAtMs = millis();
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
  bool applyWifiPsAfterPersist = false;
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
      copyCString(password, sizeof(password),
                  reusePassword ? next.staPassword : command.password);
      if (!validWifiSsid(command.ssid) ||
          !validWifiPassword(password, command.openNetwork) ||
          !validStaAddressConfig(command.staIpMode, command.staIp,
                                 command.staNetmask, command.staGateway,
                                 command.staDns1, command.staDns2)) {
        memset(password, 0, sizeof(password));
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      const bool sameCredentials =
          next.staConfigured && reusePassword &&
          strcmp(next.staSsid, command.ssid) == 0 &&
          next.staOpen == command.openNetwork &&
          next.staIpMode == command.staIpMode &&
          memcmp(next.staIp, command.staIp, sizeof(next.staIp)) == 0 &&
          memcmp(next.staNetmask, command.staNetmask,
                 sizeof(next.staNetmask)) == 0 &&
          memcmp(next.staGateway, command.staGateway,
                 sizeof(next.staGateway)) == 0 &&
          memcmp(next.staDns1, command.staDns1, sizeof(next.staDns1)) == 0 &&
          memcmp(next.staDns2, command.staDns2, sizeof(next.staDns2)) == 0;
      // Specified sleep + identical STA: persist sleep if it changed,
      // otherwise no-op. Never restart (second Save must not go PENDING).
      if (command.wifiSleepSpecified && sameCredentials) {
        memset(password, 0, sizeof(password));
        if (next.staWifiSleep != command.wifiSleep) {
          next.staWifiSleep = command.wifiSleep;
          persist = true;
          applyWifiPsAfterPersist = true;
        }
        break;
      }
      if (!command.commitConfirmed && next.staConfigured &&
          next.staConfigState ==
              static_cast<uint8_t>(StaConfigState::CONFIRMED)) {
        copyActiveStaToLkg(next);
      }
      next.staConfigured = true;
      next.staOpen = command.openNetwork;
      if (command.wifiSleepSpecified) {
        next.staWifiSleep = command.wifiSleep;
      }
      memset(next.staSsid, 0, sizeof(next.staSsid));
      memset(next.staPassword, 0, sizeof(next.staPassword));
      copyCString(next.staSsid, sizeof(next.staSsid), command.ssid);
      copyCString(next.staPassword, sizeof(next.staPassword), password);
      memset(password, 0, sizeof(password));
      next.staIpMode = command.staIpMode;
      memcpy(next.staIp, command.staIp, sizeof(next.staIp));
      memcpy(next.staNetmask, command.staNetmask, sizeof(next.staNetmask));
      memcpy(next.staGateway, command.staGateway, sizeof(next.staGateway));
      memcpy(next.staDns1, command.staDns1, sizeof(next.staDns1));
      memcpy(next.staDns2, command.staDns2, sizeof(next.staDns2));
      finalizeSavedStaCredentials(next, command.commitConfirmed);
      persist = true;
      restartPending_ = true;
      break;
    }

    case WebCommandType::FORGET_NETWORK:
      clearStaNetwork(next);
      persist = true;
      restartPending_ = true;
      break;

    case WebCommandType::CHANGE_DEVICE_PASSWORD:
      if (!setDevicePassword(next, command.password)) {
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      break;

    case WebCommandType::RESET_DEVICE_PASSWORD:
      if (!initializeDefaultDevicePassword(next)) {
        return false;
      }
      persist = true;
      apRestartPending_ = snapshot().apActive;
      log(DebugCategory::SECURITY, DebugCode::DEVICE_PASSWORD_RESET);
      break;

    case WebCommandType::RESET_NETWORK_AP:
      clearStaNetwork(next);
      if (!initializeDefaultDevicePassword(next)) {
        return false;
      }
      persist = true;
      restartPending_ = true;
      log(DebugCategory::SECURITY, DebugCode::NETWORK_RESET);
      break;

    case WebCommandType::FACTORY_RESET:
      if (callbacks_.resetAllDurableStores == nullptr) {
        return false;
      }
      if (!ensureRecoveryIntent(RecoveryOperation::FACTORY_RESET)) {
        return false;
      }
      if (!callbacks_.resetAllDurableStores(next)) {
        if (!otaRollbackRestartPending_) {
          apRestartPending_ = false;
        }
        log(DebugCategory::CONFIG, DebugCode::CONFIG_REJECTED);
        return false;
      }
      (void)clearRecoveryIntent();
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
      if (!otaRollbackRestartPending_) {
        restartPending_ = false;
      }
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
  if (applyWifiPsAfterPersist) {
    applyWifiPowerSave();
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
      if (!beginStationConnect(settings, now)) {
        actionLog("WIFI_CONNECT deferred; brew RF active");
        printActionSnapshot("WIFI_CONNECT", false);
        return true;
      }
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
      clearStaLinkMetrics();
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
      clearStaLinkMetrics();
      portEXIT_CRITICAL(&dataMux_);
      if (!beginStationConnect(settings, now)) {
        actionLog("WIFI_RESTART deferred; brew RF active");
        printActionSnapshot("WIFI_RESTART", false);
        return true;
      }
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
  if (serialDebugEnabled() && message != nullptr) {
    Serial.println(message);
  }
}

void ShotStopperNetwork::actionLogf(const char *fmt, ...) {
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
  WifiPsLive wifiPs = WifiPsLive::UNKNOWN;
  if (wifiMode != static_cast<uint8_t>(WIFI_OFF)) {
    wifi_ps_type_t ps = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&ps) == ESP_OK) {
      switch (ps) {
        case WIFI_PS_NONE:
          wifiPs = WifiPsLive::NONE;
          break;
        case WIFI_PS_MIN_MODEM:
          wifiPs = WifiPsLive::MIN_MODEM;
          break;
        case WIFI_PS_MAX_MODEM:
          wifiPs = WifiPsLive::MAX_MODEM;
          break;
        default:
          wifiPs = WifiPsLive::UNKNOWN;
          break;
      }
    }
  }
  char staMac[18] = {};
  char staBssid[18] = {};
  char apMac[18] = {};
  uint8_t mac[6] = {};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    formatWifiMac(mac, staMac, sizeof(staMac));
  }
  if (staConnected) {
    const uint8_t *bssid = WiFi.BSSID();
    if (bssid != nullptr) {
      formatWifiMac(bssid, staBssid, sizeof(staBssid));
    }
  }
  if (esp_wifi_get_mac(WIFI_IF_AP, mac) == ESP_OK) {
    formatWifiMac(mac, apMac, sizeof(apMac));
  }
  const bool ntpArm = ntpMayArm(now, staConnected);
  const bool httpActive = server_ != nullptr;
  const bool factoryPassword = passwordIsFactoryDefault(settings_);
  portENTER_CRITICAL(&dataMux_);
  status_.httpActive = httpActive;
  status_.wifiMode = wifiMode;
  status_.channel = channel;
  status_.wifiPs = wifiPs;
  status_.wifiCoex = snapshotRfCoexPreference();
  status_.wifiStatus = static_cast<int32_t>(wifiStatus);
  status_.staReconnectHeld = staReconnectHeld_;
  status_.apStartHeld = apStartHeld_;
  status_.httpStartHeld = httpStartHeld_;
  status_.devicePasswordFactory = factoryPassword;
  status_.scanState = static_cast<uint8_t>(g_wifiScan.state);
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
  memset(status_.staBssid, 0, sizeof(status_.staBssid));
  memset(status_.apMac, 0, sizeof(status_.apMac));
  copyCString(status_.staMac, sizeof(status_.staMac), staMac);
  copyCString(status_.staBssid, sizeof(status_.staBssid), staBssid);
  copyCString(status_.apMac, sizeof(status_.apMac), apMac);
  memset(status_.ntpActiveServer, 0, sizeof(status_.ntpActiveServer));
  copyCString(status_.ntpActiveServer, sizeof(status_.ntpActiveServer),
              timeStatus.activeServer);
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
  if (brewRfActive()) {
    return false;
  }
  if (scaleConnecting_.load(std::memory_order_relaxed)) {
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

  TimeStatusSnapshot timeStatus = g_wallClock.snapshot(now);

  // Brew RF or scale connecting: do not arm, and abort any in-flight SNTP
  // without counting a failure so NTP rearms when the gate clears.
  if (!ntpMayArm(now, staConnected)) {
    if (ntpStarted_ || timeStatus.state == TimeSyncState::SYNCING) {
      stopNtp();
      g_wallClock.cancelSyncing();
      ntpRearmPending_ = true;
    }
    return;
  }

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
  // Keep the httpd task stack in internal RAM (default task_caps). OTA upload
  // erases/writes flash on this task; a PSRAM stack would fault while the
  // cache is disabled.
  // Web UI serializes API traffic (DEVICE_MAX_INFLIGHT, default 1). Reserve
  // headroom for the HTML/JS/CSS boot burst plus LRU/TCP close. ESP-IDF uses
  // (max_open_sockets + 3) LWIP sockets total.
  // One active WebUI client plus a brief parallel asset fetch; 6 was oversized
  // for steady-state DRAM (each open socket carries TCP wnd/snd buffers).
  config.max_open_sockets = 4;
  // Keep one slot of headroom above the owned API routes; /api/v1/ui/unlock
  // and the four OTA routes are registered separately because they do not use
  // the exclusive WebUI claim.
  // Kept ahead of the number of routes actually registered. Exhausting this
  // makes the last registerHandler fail, which tears down the whole Web UI, so
  // check_web_assets.js fails the build before the margin is gone.
  // Shell + app.js/css/runtime + secondary + settings + 4 HTML partials + APIs.
  config.max_uri_handlers = 60;
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
      registerHandler(server_, "/stats", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/admin", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/settings", HTTP_GET, rootHandler) &&
      registerHandler(server_, "/app.js", HTTP_GET, jsHandler) &&
      registerHandler(server_, "/app.css", HTTP_GET, cssHandler) &&
      registerHandler(server_, "/js/runtime.js", HTTP_GET, runtimeJsHandler) &&
      registerHandler(server_, "/js/secondary.js", HTTP_GET,
                      secondaryJsHandler) &&
      registerHandler(server_, "/partials/stats.html", HTTP_GET,
                      partialStatsHandler) &&
      registerHandler(server_, "/partials/diagnostic.html", HTTP_GET,
                      partialDiagnosticHandler) &&
      registerHandler(server_, "/partials/settings.html", HTTP_GET,
                      partialSettingsHandler) &&
      registerHandler(server_, "/partials/admin.html", HTTP_GET,
                      partialAdminHandler) &&
      registerHandler(server_, "/js/settings.js", HTTP_GET,
                      viewSettingsHandler) &&
      registerHandler(server_, "/favicon.ico", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/apple-touch-icon.png", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/apple-touch-icon-precomposed.png", HTTP_GET,
                      browserIconHandler) &&
      registerHandler(server_, "/api/v1/ui/claim", HTTP_POST, claimHandler) &&
      registerHandler(server_, "/api/v1/ui/unlock", HTTP_POST, unlockHandler) &&
      registerHandler(server_, "/api/v1/status/home", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/settings", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/admin", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/status/diagnostic", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/debug/export", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/log", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/clear", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/delete", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/shots/rate", HTTP_POST, ownedApiHandler) &&
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
      registerHandler(server_, "/api/v1/control/state-override", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/control/restart", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/factory-reset", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_GET, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/network/scan", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/device/password", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/unlock", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/lock", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/admin/ble-compat", HTTP_PUT, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/diagnostic/profiler", HTTP_POST, ownedApiHandler) &&
      registerHandler(server_, "/api/v1/ota", HTTP_GET, otaStatusHandler) &&
      registerHandler(server_, "/api/v1/ota", HTTP_POST, otaUploadHandler) &&
      registerHandler(server_, "/api/v1/ota/flash", HTTP_POST, otaFlashHandler) &&
      registerHandler(server_, "/api/v1/ota/abort", HTTP_POST, otaAbortHandler);
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
  const size_t length = json == nullptr ? 0 : strlen(json);
  return sendCopiedBody(request, json, length);
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

const char *configLockReason(const ControlGateSnapshot &status) {
  if (status.activeCycle) return "active_shot";
  if (status.machineRunning) return "machine_running";
  if (status.physicalActivatorOn) return "activator_on";
  if (status.maintenanceLeaseActive) return "maintenance";
  if (status.state != StopperState::READY) return "not_ready";
  return "none";
}

const char *configLockReason(const ControlStatusSnapshot &status) {
  return configLockReason(controlGateOf(status));
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
  self.webUiOverrideUntilMs_ = 0;
  portEXIT_CRITICAL(&self.dataMux_);
  self.clearAdminUnlock();
  memset(clientId, 0, sizeof(clientId));
  return sendJson(request, STATUS_OK, "{\"active\":true}");
}

esp_err_t ShotStopperNetwork::unlockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireActiveWebUiClient(request)) return ESP_OK;
  if (!self.requireAdminUnlock(request)) return ESP_OK;
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  char confirmation[32] = {};
  static const char *const fields[] = {"confirm"};
  const bool confirmed = root != nullptr &&
                         jsonHasOnlyUniqueFields(root, fields, 1) &&
                         jsonString(root, "confirm", confirmation,
                                    sizeof(confirmation), false) &&
                         strcmp(confirmation, "UNSAFE_WEBUI_OVERRIDE") == 0;
  if (root != nullptr) cJSON_Delete(root);
  self.unlockJsonBody();
  memset(confirmation, 0, sizeof(confirmation));
  if (!confirmed) {
    return sendError(request, STATUS_UNPROCESSABLE, "UNLOCK_NOT_CONFIRMED",
                     "The unsafe WebUI override was not explicitly confirmed.");
  }
  const uint32_t now = millis();
  portENTER_CRITICAL(&self.dataMux_);
  self.webUiOverrideActive_ = true;
  self.webUiOverrideUntilMs_ = now + WEB_UI_OVERRIDE_MS;
  portEXIT_CRITICAL(&self.dataMux_);
  return sendJson(request, STATUS_OK, "{\"unlocked\":true}");
}

bool ShotStopperNetwork::webUiOverrideAllowed(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) return false;
  const uint32_t now = millis();
  bool allowed = false;
  portENTER_CRITICAL(&dataMux_);
  if (webUiOverrideActive_ && activeWebUiClientId_[0] != '\0' &&
      strcmp(activeWebUiClientId_, clientId) == 0) {
    if (static_cast<int32_t>(now - webUiOverrideUntilMs_) >= 0) {
      webUiOverrideActive_ = false;
      webUiOverrideUntilMs_ = 0;
    } else {
      allowed = true;
    }
  }
  portEXIT_CRITICAL(&dataMux_);
  memset(clientId, 0, sizeof(clientId));
  return allowed;
}

uint32_t ShotStopperNetwork::webUiOverrideRemainingMs(httpd_req_t *request) {
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) return 0;
  const uint32_t now = millis();
  uint32_t remaining = 0;
  portENTER_CRITICAL(&dataMux_);
  if (webUiOverrideActive_ && activeWebUiClientId_[0] != '\0' &&
      strcmp(activeWebUiClientId_, clientId) == 0 &&
      static_cast<int32_t>(now - webUiOverrideUntilMs_) < 0) {
    remaining = webUiOverrideUntilMs_ - now;
  }
  portEXIT_CRITICAL(&dataMux_);
  memset(clientId, 0, sizeof(clientId));
  return remaining;
}

bool ShotStopperNetwork::webUiConfigurationAllowed(
    httpd_req_t *request, const ControlGateSnapshot &status) {
  if (controlAllowsConfiguration(status)) {
    return true;
  }
  return webUiOverrideAllowed(request);
}

bool ShotStopperNetwork::historyMutationAllowed(
    httpd_req_t *request, const ControlGateSnapshot &status) {
  if (!controlAllowsHistoryMutation(status)) {
    return false;
  }
  return webUiConfigurationAllowed(request, status);
}

void ShotStopperNetwork::clearAdminUnlock() {
  portENTER_CRITICAL(&dataMux_);
  adminUnlocked_ = false;
  adminUnlockClientId_[0] = '\0';
  adminUnlockUntilMs_ = 0;
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::grantAdminUnlock(const char *clientId, uint32_t now) {
  if (clientId == nullptr) {
    return;
  }
  portENTER_CRITICAL(&dataMux_);
  memcpy(adminUnlockClientId_, clientId, sizeof(adminUnlockClientId_));
  adminUnlocked_ = true;
  adminUnlockUntilMs_ = now + ADMIN_UNLOCK_IDLE_MS;
  adminUnlockFailures_ = 0;
  adminUnlockCooldownUntilMs_ = 0;
  portEXIT_CRITICAL(&dataMux_);
}

void ShotStopperNetwork::touchAdminUnlock() {
  const uint32_t now = millis();
  portENTER_CRITICAL(&dataMux_);
  if (adminUnlocked_) {
    adminUnlockUntilMs_ = now + ADMIN_UNLOCK_IDLE_MS;
  }
  portEXIT_CRITICAL(&dataMux_);
}

bool ShotStopperNetwork::adminUnlockAllowed(httpd_req_t *request) {
#if SHOT_STOPPER_DEVELOPMENT == 1
  (void)request;
  return true;
#else
  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    return false;
  }
  const uint32_t now = millis();
  bool allowed = false;
  portENTER_CRITICAL(&dataMux_);
  if (adminUnlocked_ && adminUnlockClientId_[0] != '\0' &&
      strcmp(adminUnlockClientId_, clientId) == 0) {
    if (static_cast<int32_t>(now - adminUnlockUntilMs_) >= 0) {
      adminUnlocked_ = false;
      adminUnlockClientId_[0] = '\0';
      adminUnlockUntilMs_ = 0;
    } else {
      allowed = true;
    }
  }
  portEXIT_CRITICAL(&dataMux_);
  memset(clientId, 0, sizeof(clientId));
  return allowed;
#endif
}

bool ShotStopperNetwork::requireAdminUnlock(httpd_req_t *request) {
  if (!adminUnlockAllowed(request)) {
    sendError(request, STATUS_UNAUTHORIZED, "ADMIN_LOCKED",
              "Unlock administration with the device password first.");
    return false;
  }
  touchAdminUnlock();
  return true;
}

esp_err_t ShotStopperNetwork::adminUnlockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char password[WIFI_PASSWORD_CAPACITY] = {};
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"password"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "password", password, sizeof(password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    memset(password, 0, sizeof(password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "password is required.");
  }

  char clientId[WEB_UI_CLIENT_ID_CAPACITY] = {};
  if (!readWebUiClientId(request, clientId, sizeof(clientId))) {
    memset(password, 0, sizeof(password));
    return sendError(request, STATUS_BAD_REQUEST, "UI_CLIENT_INVALID",
                     "X-WebUI-Client must be a 16-24 character lowercase hex id.");
  }

  const uint32_t now = millis();
  bool coolingDown = false;
  portENTER_CRITICAL(&self.dataMux_);
  coolingDown = self.adminUnlockCooldownUntilMs_ != 0 &&
                static_cast<int32_t>(now - self.adminUnlockCooldownUntilMs_) <
                    0;
  portEXIT_CRITICAL(&self.dataMux_);
  if (coolingDown) {
    memset(password, 0, sizeof(password));
    memset(clientId, 0, sizeof(clientId));
    return sendError(request, STATUS_TOO_MANY, "ADMIN_UNLOCK_COOLDOWN",
                     "Too many failed unlock attempts. Try again shortly.");
  }

  char expected[WIFI_PASSWORD_CAPACITY] = {};
  portENTER_CRITICAL(&self.dataMux_);
  memcpy(expected, self.settings_.devicePassword, sizeof(expected));
  portEXIT_CRITICAL(&self.dataMux_);
  const bool matches = secretsMatch(password, expected);
  memset(password, 0, sizeof(password));
  memset(expected, 0, sizeof(expected));
  if (!matches) {
    uint8_t failures = 0;
    portENTER_CRITICAL(&self.dataMux_);
    if (self.adminUnlockFailures_ < 255) {
      self.adminUnlockFailures_ =
          static_cast<uint8_t>(self.adminUnlockFailures_ + 1);
    }
    failures = self.adminUnlockFailures_;
    if (failures >= ADMIN_UNLOCK_FAILURES_BEFORE_COOLDOWN) {
      self.adminUnlockCooldownUntilMs_ = now + ADMIN_UNLOCK_COOLDOWN_MS;
      self.adminUnlockFailures_ = 0;
    }
    portEXIT_CRITICAL(&self.dataMux_);
    memset(clientId, 0, sizeof(clientId));
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Device password is incorrect.");
  }

  self.grantAdminUnlock(clientId, now);
  memset(clientId, 0, sizeof(clientId));
  return sendJson(request, STATUS_OK, "{\"unlocked\":true}");
}

esp_err_t ShotStopperNetwork::adminLockHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  self.unlockJsonBody();
  self.clearAdminUnlock();
  return sendJson(request, STATUS_OK, "{\"unlocked\":false}");
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
  if (apiUriMatches(request->uri, "/api/v1/debug/export")) {
    return debugExportHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/log")) return logHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots")) return shotsHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/clear")) return shotsClearHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/delete")) return shotsDeleteHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/shots/rate")) return shotsRateHandler(request);
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
  if (apiUriMatches(request->uri, "/api/v1/control/state-override")) return stateOverrideHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/control/restart")) return restartHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/factory-reset")) return factoryResetHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/network/scan")) {
    return request->method == HTTP_GET ? wifiScanStatusHandler(request)
                                       : wifiScanStartHandler(request);
  }
  if (apiUriMatches(request->uri, "/api/v1/network")) return networkHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/device/password")) return devicePasswordHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/unlock")) return adminUnlockHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/lock")) return adminLockHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/admin/ble-compat")) return bleCompatHandler(request);
  if (apiUriMatches(request->uri, "/api/v1/diagnostic/profiler")) return taskProfilerHandler(request);
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

static esp_err_t serveImmutableGzip(httpd_req_t *request, const char *contentType,
                                    const uint8_t *data, size_t length) {
  // WEB_UI_ETAG is bake-time (FW_VERSION + asset tag); immutable ?v= busts
  // after reflashes without snprintf on each request.
  if (ifNoneMatchEquals(request, WEB_UI_ETAG)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control",
                       "public, max-age=31536000, immutable");
    httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, contentType);
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Connection", "close");
  return sendCopiedBody(request, data, length);
}

esp_err_t ShotStopperNetwork::rootHandler(httpd_req_t *request) {
  if (ifNoneMatchEquals(request, WEB_UI_ETAG)) {
    httpd_resp_set_status(request, STATUS_NOT_MODIFIED);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, nullptr, 0);
  }
  httpd_resp_set_type(request, "text/html; charset=utf-8");
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(request, "ETag", WEB_UI_ETAG);
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(
      request, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; "
      "connect-src 'self'; frame-ancestors 'none'");
  httpd_resp_set_hdr(request, "Connection", "close");
  return sendCopiedBody(request, SHOT_STOPPER_WEB_UI_GZIP,
                        SHOT_STOPPER_WEB_UI_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::jsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_JS_GZIP,
                            SHOT_STOPPER_WEB_JS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::cssHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/css; charset=utf-8",
                            SHOT_STOPPER_WEB_CSS_GZIP,
                            SHOT_STOPPER_WEB_CSS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::runtimeJsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_RUNTIME_GZIP,
                            SHOT_STOPPER_WEB_RUNTIME_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::secondaryJsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_SECONDARY_GZIP,
                            SHOT_STOPPER_WEB_SECONDARY_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialStatsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_STATS_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_STATS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialDiagnosticHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_DIAGNOSTIC_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_DIAGNOSTIC_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialSettingsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::partialAdminHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "text/html; charset=utf-8",
                            SHOT_STOPPER_WEB_PARTIAL_ADMIN_GZIP,
                            SHOT_STOPPER_WEB_PARTIAL_ADMIN_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::viewSettingsHandler(httpd_req_t *request) {
  return serveImmutableGzip(request, "application/javascript; charset=utf-8",
                            SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP,
                            SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP_LEN);
}

esp_err_t ShotStopperNetwork::browserIconHandler(httpd_req_t *request) {
  // Safari/iOS probe these paths on every visit. A registered handler avoids
  // the IDF "URI not found" WARN and the 302-to-/ HTML bounce.
  httpd_resp_set_status(request, STATUS_NO_CONTENT);
  httpd_resp_set_hdr(request, "Cache-Control",
                     "public, max-age=31536000, immutable");
  httpd_resp_set_hdr(request, "Connection", "close");
  return httpd_resp_send(request, nullptr, 0);
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
    case EndReason::ACTIVATOR: return "ACTIVATOR";
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
    case EndReason::CUP_REMOVED:
      return "CUP_REMOVED";
    case EndReason::UNCONFIRMED_START:
      return "UNCONFIRMED_START";
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
  // Home/settings stay claim-gated only. Admin and Diagnostic mutations and
  // the full privileged status body require a temporary device-password unlock.
  self.requestPendingNetworkConfirm();

  const StatusPage page = parseStatusPage(request->uri);
  if (page == StatusPage::Unknown) {
    return sendError(request, "404 Not Found", "STATUS_PAGE_UNKNOWN",
                     "Unknown status page; use /api/v1/status/{home|settings|admin|diagnostic}.");
  }

  // JSON / snapshot work lives in one PSRAM (or internal fallback) blob.
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }

  ControlStatusSnapshot &control = self.workBuf_->control;
  self.loadControlStatus(control);
  const bool configMutable = controlAllowsConfiguration(control);
  const bool webUiOverrideActive = self.webUiOverrideAllowed(request);
  const uint32_t webUiOverrideRemainingMs =
      webUiOverrideActive ? self.webUiOverrideRemainingMs(request) : 0;
  bool adminUnlocked = false;
  if (page == StatusPage::Admin || page == StatusPage::Home ||
      page == StatusPage::Diagnostic) {
    adminUnlocked = self.adminUnlockAllowed(request);
    if (adminUnlocked &&
        (page == StatusPage::Admin || page == StatusPage::Diagnostic)) {
      self.touchAdminUnlock();
    }
  }
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());

  char currentWeight[32] = "null";
  char observedWeight[32] = "null";
  char lastShotWeight[32] = "null";
  char scaleTimer[32] = "null";
  char staRssiJson[16] = "null";
  char staSignalQualityJson[16] = "null";
  char staChannelJson[8] = "null";
  char scaleRssiJson[16] = "null";
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
  if (control.scaleRssiValid) {
    snprintf(scaleRssiJson, sizeof(scaleRssiJson), "%d",
             static_cast<int>(control.scaleRssi));
  }

  char safeNtpCustom[NTP_SERVER_HOST_CAPACITY] = {};
  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  char safeScaleProtocol[24] = {};
  char safeLastShotProtocol[24] = {};
  char safePreferredScaleMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
  char safePreferredScaleName[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
  char safeFirmwareVersion[32] = {};
  char safeStaSsid[WIFI_SSID_CAPACITY] = {};
  char safeBullseyeRtttl[BULLSEYE_RTTTL_CAPACITY] = {};
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
  if (page == StatusPage::Settings &&
      self.callbacks_.copyBullseyeConfig != nullptr) {
    self.callbacks_.copyBullseyeConfig(&g_work->bullseyeMelody);
    sanitizeJsonEmbed(g_work->bullseyeMelody.rtttl, safeBullseyeRtttl,
                      sizeof(safeBullseyeRtttl));
  }

  const bool needPresets =
      page == StatusPage::Home || page == StatusPage::Settings;
  const bool needHistory = page == StatusPage::Settings;
  if (needPresets) {
    if (self.callbacks_.copyPresetBank != nullptr) {
      self.callbacks_.copyPresetBank(&g_work->presetBank);
    } else {
      memset(&g_work->presetBank, 0, sizeof(g_work->presetBank));
    }
    buildSlimPresetsJson(g_work->presetBank);
  } else {
    g_work->presetsJson[0] = '{';
    g_work->presetsJson[1] = '}';
    g_work->presetsJson[2] = 0;
  }
  if (needHistory) {
    if (self.callbacks_.copyScaleHistory != nullptr) {
      self.callbacks_.copyScaleHistory(g_work->scaleHistory);
    } else {
      memset(g_work->scaleHistory, 0, sizeof(g_work->scaleHistory));
    }
    buildScaleHistoryJson(g_work->scaleHistory);
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
      "{\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"configMutable\":%s,"
      "\"webUiOverrideActive\":%s,\"webUiOverrideRemainingMs\":%lu,"
      "\"configLockReason\":\"%s\",\"liveShot\":%s",
      safeFirmwareVersion,
      static_cast<unsigned long>(control.bootId),
      configMutable ? "true" : "false",
      webUiOverrideActive ? "true" : "false",
      static_cast<unsigned long>(webUiOverrideRemainingMs),
      configLockReason(control), liveShot ? "true" : "false");
  if (ok) {
    ok = statusJsonAppend(&used, ",\"machineType\":\"%s\"",
                          compiledMachineTypeId());
  }
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
  if (ok && page == StatusPage::Admin && adminUnlocked) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"ntpServerPreset\":\"%s\","
        "\"ntpServerCustom\":\"%s\"",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        ntpPresetId(control.config.ntpServerPreset), safeNtpCustom);
  }
  if (ok && page == StatusPage::Diagnostic && adminUnlocked) {
    ok = statusJsonAppend(
        &used,
        ",\"timezoneOffsetMinutes\":%d,\"serialDebugOutput\":%s",
        static_cast<int>(control.config.timezoneOffsetMinutes),
        control.config.serialDebugOutput ? "true" : "false");
  }

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"soundAlertsEnabled\":%s,\"alertOutputChannel\":\"%s\","
        "\"brewByWeight\":%s,\"goalWeightG\":%u,"
        "\"operationalWallMs\":%lu,\"minBbwBrewTimeMs\":%lu,\"maxBbwBrewTimeMs\":%lu,"
        "\"minRecoveryWeightG\":%.1f,\"maxRecoveryWeightG\":%.1f,"
        "\"fastExtractionGuardEnabled\":%s,\"slowExtractionGuardEnabled\":%s,"
        "\"autoToManualGuardEnabled\":%s,\"cupProtectionEnabled\":%s,"
        "\"stopIfCupRemoved\":%s,\"requireCupToStart\":%s,"
        "\"avoidAccidentalTouchEnabled\":%s,"
        "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
        "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
        "\"scaleMacCacheMode\":\"%s\"",
        control.config.soundAlertsMuted ? "false" : "true",
        alertOutputChannelId(control.config.alertOutputChannel),
        control.config.timerOnly ? "false" : "true",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<unsigned long>(control.config.operationalWallMs),
        static_cast<unsigned long>(control.config.minBbwBrewTimeMs),
        static_cast<unsigned long>(control.config.maxBbwBrewTimeMs),
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<double>(control.config.maxRecoveryWeightG),
        control.config.fastExtractionGuardEnabled ? "true" : "false",
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        control.config.autoToManualGuardEnabled ? "true" : "false",
        control.config.cupProtectionEnabled ? "true" : "false",
        control.config.stopIfCupRemoved ? "true" : "false",
        control.config.requireCupToStart ? "true" : "false",
        control.config.avoidAccidentalTouchEnabled ? "true" : "false",
        static_cast<double>(control.config.cupPresentWeightG),
        static_cast<double>(control.config.cupRemovedWeightG),
        noScaleBbwEnabled(control.config.noScaleBbwMode) ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        scaleMacCacheModeId(control.config.scaleMacCacheMode));
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"goalWeightG\":%u,\"weightOffsetG\":%.2f,"
        "\"weightOffsetBaselineG\":%.2f,\"autoTare\":%s,"
        "\"postTareBaselineGraceMs\":%lu,\"brewByWeight\":%s,"
        "\"canTareStartTimer\":%s,\"scaleTimerStopExtraDelayMs\":%lu,"
        "\"dripDelayMs\":%lu,"
        "\"soundAlertsEnabled\":%s,\"firstDropBeep\":%s,"
        "\"paddleReturnReminderBeep\":%s,"
        "\"paddleReturnReminderIntervalMs\":%lu,"
        "\"paddleReturnReminderMaxDurationMs\":%lu,\"paddleMode\":\"%s\","
        "\"stopPulseMs\":%lu,\"maxSinglePressMs\":%lu,"
        "\"momentaryStartEdge\":\"%s\",\"reedConfirmTimeoutMs\":%lu,"
        "\"assumeIdleWhenScaleConnects\":%s,\"shotReactTimeoutS\":%u,"
        "\"buzzerScaleLostBeep\":%s,\"buzzerAutoToManualGuardEndBeep\":%s,"
        "\"buzzerManualNoScaleBeep\":%s,\"buzzerScaleConnectedBeep\":%s,"
        "\"scaleConnectedLed\":%s,"
        "\"buzzerExtendedPulseRate\":\"%s\","
        "\"buzzerSlowExtendedPulseRate\":\"%s\","
        "\"alertOutputChannel\":\"%s\",\"rinseEnabled\":%s,\"rinseGestureMs\":%lu,"
        "\"rinseDurationMs\":%lu,\"autoRetare\":%s,\"retareWindowMs\":%lu,"
        "\"minimumCupWeightG\":%.1f,\"retareStabilitySamples\":%u,"
        "\"retareStabilityToleranceG\":%.1f,\"retareStabilityMaxGapMs\":%lu,"
        "\"retareStabilityMinDurationMs\":%lu,\"bbwProtectionMs\":%lu,"
        "\"operationalWallMs\":%lu,\"fastExtractionGuardEnabled\":%s,"
        "\"maxRecoveryWeightG\":%.1f,\"minBbwBrewTimeMs\":%lu,"
        "\"slowExtractionGuardEnabled\":%s,\"minRecoveryWeightG\":%.1f,"
        "\"maxBbwBrewTimeMs\":%lu,\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardLimitMode\":\"%s\","
        "\"autoToManualGuardManualLimitMs\":%lu,"
        "\"autoToManualGuardBaselineMs\":%lu,"
        "\"autoToManualGuardTrendMs\":%lu,\"cupProtectionEnabled\":%s,"
        "\"stopIfCupRemoved\":%s,\"requireCupToStart\":%s,"
        "\"avoidAccidentalTouchEnabled\":%s,"
        "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
        "\"scaleMacCacheMode\":\"%s\","
        "\"bookooMuteOnBuzzerOnly\":%s,\"bookooConnectBeepLevel\":%u,"
        "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
        "\"lastShotCooldownMs\":%lu",
        static_cast<unsigned>(control.config.goalWeightG),
        static_cast<double>(control.config.weightOffsetG),
        static_cast<double>(control.config.weightOffsetBaselineG),
        control.config.autoTare ? "true" : "false",
        static_cast<unsigned long>(control.config.postTareBaselineGraceMs),
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
        static_cast<unsigned long>(runtimeStopPulseMs(control.config)),
        static_cast<unsigned long>(runtimeMaxSinglePressMs(control.config)),
        momentaryStartEdgeId(control.config.momentaryStartOnPress),
        static_cast<unsigned long>(
            runtimeReedConfirmTimeoutMs(control.config)),
        control.config.assumeIdleWhenScaleConnects ? "true" : "false",
        static_cast<unsigned>(runtimeShotReactTimeoutS(control.config)),
        control.config.buzzerScaleLostBeep ? "true" : "false",
        control.config.buzzerAutoToManualGuardEndBeep ? "true" : "false",
        control.config.buzzerManualNoScaleBeep ? "true" : "false",
        control.config.buzzerScaleConnectedBeep ? "true" : "false",
        control.config.scaleConnectedLed ? "true" : "false",
        extendedPulseRateId(control.config.buzzerExtendedPulseRate),
        extendedPulseRateId(control.config.buzzerSlowExtendedPulseRate),
        alertOutputChannelId(control.config.alertOutputChannel),
        control.config.rinseEnabled ? "true" : "false",
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
        static_cast<unsigned long>(control.config.minBbwBrewTimeMs),
        control.config.slowExtractionGuardEnabled ? "true" : "false",
        static_cast<double>(control.config.minRecoveryWeightG),
        static_cast<unsigned long>(control.config.maxBbwBrewTimeMs),
        control.config.autoToManualGuardEnabled ? "true" : "false",
        autoToManualGuardLimitModeId(control.config.autoToManualGuardLimitMode),
        static_cast<unsigned long>(control.config.autoToManualGuardManualLimitMs),
        static_cast<unsigned long>(control.config.autoToManualGuardBaselineMs),
        static_cast<unsigned long>(control.autoToManualGuardTrendMs),
        control.config.cupProtectionEnabled ? "true" : "false",
        control.config.stopIfCupRemoved ? "true" : "false",
        control.config.requireCupToStart ? "true" : "false",
        control.config.avoidAccidentalTouchEnabled ? "true" : "false",
        static_cast<double>(control.config.cupPresentWeightG),
        static_cast<double>(control.config.cupRemovedWeightG),
        scaleMacCacheModeId(control.config.scaleMacCacheMode),
        control.config.bookooMuteOnBuzzerOnly ? "true" : "false",
        static_cast<unsigned>(control.config.bookooConnectBeepLevel),
        noScaleBbwEnabled(control.config.noScaleBbwMode) ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        static_cast<unsigned long>(control.config.lastShotCooldownMs));
  }

  if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used, ",\"bullseyeMelodyEnabled\":%s,\"bullseyeRtttl\":\"%s\"",
        g_work->bullseyeMelody.enabled ? "true" : "false",
        safeBullseyeRtttl);
  }

  if (ok) {
    ok = statusJsonAppend(&used, "}");
  }

  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
  }
  if (ok && page == StatusPage::Home) {
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"stateLabel\":\"%s\",\"machineState\":\"%s\","
        "\"relayClosed\":%s,\"machineRunning\":%s,"
        "\"physicalActivatorOn\":%s,\"virtualHoldOn\":%s,"
        "\"remoteControlEnabled\":%s,\"controlSource\":\"%s\","
        "\"circuitElapsedMs\":%lu,"
        "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
        "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
        "\"recoveryRequired\":%s},"
        "\"scale\":{\"available\":%s,\"protocol\":\"%s\","
        "\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"controlAccepted\":%s,\"currentWeightG\":%s,"
        "\"observedWeightG\":%s,\"timerMs\":%s,"
        "\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,"
        "\"lastDisconnectReasonName\":\"%s\"},"
        "\"presets\":%s,"
        "\"cycle\":{\"active\":%s,\"shotType\":\"%s\","
        "\"retarePerformed\":%s,\"firstDropElapsedMs\":%lu,"
        "\"extractionExtended\":%s,\"slowExtractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"minBbwBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu,"
        "\"accidentalTouchHolding\":%s},"
        "\"lastShot\":{\"valid\":%s,\"currentWeightG\":%s,"
        "\"goalWeightG\":%u,\"extractionExtended\":%s,"
        "\"activeStopWeightG\":%.1f,\"durationMs\":%lu,"
        "\"firstDropElapsedMs\":%lu,\"retarePerformed\":%s,"
        "\"shotType\":\"%s\",\"scaleProtocol\":\"%s\","
        "\"scaleAvailable\":%s,\"fastExtractionGuardEnabled\":%s,"
        "\"slowExtractionGuardEnabled\":%s,\"slowExtractionExtended\":%s,"
        "\"minBbwBrewTimeRemainingMs\":%lu,"
        "\"autoToManualGuardEnabled\":%s,"
        "\"autoToManualGuardArmed\":%s,"
        "\"autoToManualGuardEnforced\":%s,"
        "\"autoToManualGuardRemainingMs\":%lu,"
        "\"noScaleShotGuardEnabled\":%s,"
        "\"noScaleShotGuardArmed\":%s,\"noScaleBbwMode\":\"%s\","
        "\"endReason\":\"%s\","
        "\"rating\":%u,\"shotLogId\":%lu},"
        "\"noScaleShotGuard\":{\"enabled\":%s,\"armed\":%s,"
        "\"mode\":\"%s\",\"cooldownRemainingMs\":%lu,"
        "\"scaleUsable\":%s},"
        "\"cupPresence\":{\"state\":\"%s\",\"present\":%s}",
        stopperStateName(control.state), stateLabel(control.state),
        machineRunStateName(control.machineRunState),
        control.relayClosed ? "true" : "false",
        control.machineRunning ? "true" : "false",
        control.physicalActivatorOn ? "true" : "false",
        control.virtualHoldOn ? "true" : "false",
        control.remoteControlEnabled ? "true" : "false",
        controlSourceName(control.source),
        static_cast<unsigned long>(control.circuitElapsedMs),
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
        scaleDisconnectReasonName(control.scaleLastDisconnectReason),
        g_work->presetsJson, control.activeCycle ? "true" : "false",
        activeCycleShotTypeLabel(control),
        control.cycleRetarePerformed ? "true" : "false",
        static_cast<unsigned long>(cycleFirstDropElapsedMs),
        control.cycleExtractionExtended ? "true" : "false",
        control.cycleSlowExtractionExtended ? "true" : "false",
        static_cast<double>(control.cycleActiveStopWeightG),
        static_cast<unsigned long>(control.cycleMinBbwBrewTimeRemainingMs),
        control.cycleAutoToManualGuardArmed ? "true" : "false",
        control.cycleAutoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(control.cycleAutoToManualGuardRemainingMs),
        control.cycleAccidentalTouchHolding ? "true" : "false",
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
        static_cast<unsigned long>(control.lastShot.minBbwBrewTimeRemainingMs),
        control.lastShot.autoToManualGuardEnabled ? "true" : "false",
        control.lastShot.autoToManualGuardArmed ? "true" : "false",
        control.lastShot.autoToManualGuardEnforced ? "true" : "false",
        static_cast<unsigned long>(
            control.lastShot.autoToManualGuardRemainingMs),
        control.lastShot.noScaleShotGuardEnabled ? "true" : "false",
        control.lastShot.noScaleShotGuardArmed ? "true" : "false",
        noScaleBbwModeId(
            control.lastShot.noScaleShotGuardEnabled &&
                    control.lastShot.noScaleBbwMode ==
                        static_cast<uint8_t>(NoScaleBbwMode::OFF)
                ? static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE)
                : control.lastShot.noScaleBbwMode),
        endReasonName(control.lastShot.endReason),
        static_cast<unsigned>(control.lastShot.rating),
        static_cast<unsigned long>(control.lastShot.shotLogId),
        control.noScaleShotGuardEnabled ? "true" : "false",
        control.noScaleShotGuardArmed ? "true" : "false",
        noScaleBbwModeId(control.config.noScaleBbwMode),
        static_cast<unsigned long>(
            control.noScaleShotGuardCooldownRemainingMs),
        (control.scaleAvailable &&
         control.weightStreamState == WeightStreamState::FRESH)
            ? "true"
            : "false",
        cupPresenceStateName(control.cupPresenceState),
        control.cupPresent ? "true" : "false");
    if (ok) {
      const ShotCurveRecord curve = shotCurveRecordFromStatusFields(
          control.shotCurveCount, control.shotCurveIntervalS,
          control.shotCurveFirstDropDs, control.shotCurveFirstDropCg,
          control.shotCurveExtendedDs, control.shotCurveExtendedCg,
          control.shotCurveAtmDs, control.shotCurveAtmCg,
          control.shotCurveAtmClearedDs, control.shotCurveEndedDs,
          control.shotCurveEndedCg, control.shotCurveWeightCg,
          sizeof(control.shotCurveWeightCg) /
              sizeof(control.shotCurveWeightCg[0]));
      char curveJson[640] = {};
      if (formatShotCurveJsonBody(curveJson, sizeof(curveJson), curve)) {
        ok = statusJsonAppend(&used, ",\"shotCurve\":{%s}", curveJson);
      }
    }
  } else if (ok && page == StatusPage::Settings) {
    ok = statusJsonAppend(
        &used,
        ",\"scale\":{\"preferredMac\":\"%s\",\"preferredName\":\"%s\","
        "\"macCachePauseRemainingMs\":%lu,\"history\":%s},"
        "\"presets\":%s,\"stopPulseMs\":%lu,\"maxSinglePressMs\":%lu,"
        "\"momentaryStartEdge\":\"%s\",\"reedConfirmTimeoutMs\":%lu,"
        "\"assumeIdleWhenScaleConnects\":%s,\"shotReactTimeoutS\":%u",
        safePreferredScaleMac, safePreferredScaleName,
        static_cast<unsigned long>(control.scaleMacCachePauseRemainingMs),
        g_work->historyJson, g_work->presetsJson,
        static_cast<unsigned long>(runtimeStopPulseMs(control.config)),
        static_cast<unsigned long>(runtimeMaxSinglePressMs(control.config)),
        momentaryStartEdgeId(control.config.momentaryStartOnPress),
        static_cast<unsigned long>(
            runtimeReedConfirmTimeoutMs(control.config)),
        control.config.assumeIdleWhenScaleConnects ? "true" : "false",
        static_cast<unsigned>(runtimeShotReactTimeoutS(control.config)));
  } else if (ok && page == StatusPage::Admin) {
    // Locked admin: confirm-window hint only. Unlocked: Wi-Fi/AP + BLE + OTA.
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
    if (ok && !adminUnlocked) {
      ok = statusJsonAppend(
          &used,
          ",\"network\":{\"configState\":\"%s\",\"confirmRemainingMs\":%lu}",
          staConfigStateName(network.staConfigState),
          static_cast<unsigned long>(network.confirmRemainingMs));
    } else if (ok) {
      // Admin page: Wi-Fi/AP status + STA address form hydration only.
      // Diagnostics metrics live on status/diagnostic.
      ok = statusJsonAppend(
          &used,
          ",\"network\":{\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
          "\"wifiConfigured\":%s,\"ssid\":\"%s\",\"open\":%s,\"wifiSleep\":%s,"
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
          "\"lastReject\":\"%s\",\"scanIntensity\":\"%s\"}",
          network.apActive ? "true" : "false", network.apIp,
          static_cast<unsigned>(network.apClients),
          network.wifiConfigured ? "true" : "false", safeStaSsid,
          network.staOpen ? "true" : "false",
          network.staWifiSleep ? "true" : "false", staStateName(network.staState),
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
              control.bleCompanionLastReject)),
          bleScanIntensityName(clampBleScanIntensity(
              control.bleCompanionScanIntensity)));
      if (ok) {
        self.buildOtaJson(g_work->otaJson, NetworkWorkBuf::kOtaJson,
                          controlGateOf(control));
        ok = statusJsonAppend(&used, ",\"boardArch\":\"%s\",\"ota\":%s",
                              FW_BOARD_ARCH_STRING, g_work->otaJson);
      }
    }
  } else if (ok && page == StatusPage::Diagnostic) {
    ok = statusJsonAppend(&used, ",\"adminUnlocked\":%s,\"development\":%s",
                          adminUnlocked ? "true" : "false",
                          DEVELOPMENT_BUILD ? "true" : "false");
    if (ok && adminUnlocked) {
    // Lean diagnostic snapshot: metrics + log controls only (no STA address
    // form fields; those stay on status/admin).
    ok = statusJsonAppend(
        &used,
        ",\"state\":\"%s\",\"machineState\":\"%s\","
        "\"relayClosed\":%s,\"machineRunning\":%s,"
        "\"machineStartAck\":%s,\"machineStopAck\":%s,\"machineOrphan\":%s,"
        "\"reedOn\":%s,"
        "\"physicalActivatorOn\":%s,"
        "\"controlSource\":\"%s\","
        "\"cupPresence\":{\"state\":\"%s\",\"present\":%s},"
        "\"network\":{\"apActive\":%s,\"apIp\":\"%s\",\"apClients\":%u,"
        "\"wifiConfigured\":%s,\"ssid\":\"%s\","
        "\"staState\":\"%s\",\"wifiPs\":\"%s\",\"wifiCoex\":\"%s\",\"channel\":%s,\"staIp\":\"%s\",\"ipMode\":\"%s\","
        "\"configState\":\"%s\",\"confirmRemainingMs\":%lu,"
        "\"rssi\":%s,\"signalQualityPct\":%s},"
        "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
        "\"nextRetryInMs\":%lu,\"activeServer\":\"%s\"},"
        "\"maintenance\":{\"active\":%s,\"leaseId\":%lu,"
        "\"persistPending\":%s,\"persistFailed\":%s},"
        "\"health\":{\"uptimeMs\":%lu,\"loopIntervalGapMs\":%lu,"
        "\"loopMaxGapMs\":%lu,"
        "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
        "\"largestFreeHeapBlockBytes\":%lu,"
        "\"psramSizeBytes\":%lu,\"psramFreeBytes\":%lu,"
        "\"psramLargestFreeBlockBytes\":%lu,"
        "\"bleHostAllocPsram\":%lu,\"bleHostAllocFallback\":%lu,"
        "\"hciRxDropped\":%lu,\"hciTxDropped\":%lu,"
        "\"workBufExternal\":%s,\"jsonArenaExternal\":%s,"
        "\"allocExternalFallback\":%lu,"
        "\"hwmon\":{\"cpuLoad5s\":%.2f,\"cpuLoad1m\":%.2f,\"cpuLoad5m\":%.2f,"
        "\"cpu0Busy\":%.2f,\"cpu1Busy\":%.2f,\"cpuLoadValid\":%s,"
        "\"cpuMhz\":%lu,"
        "\"tempValid\":%s,\"tempC\":%.1f,\"tempPeakC\":%.1f,"
        "\"ramTotalBytes\":%lu,\"ramUsedBytes\":%lu,"
        "\"ramFreeBytes\":%lu}},"
        "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
        "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
        "\"recoveryRequired\":%s,\"resetReasonCode\":%lu},"
        "\"scale\":{\"streamState\":\"%s\",\"controlState\":\"%s\","
        "\"packetGaps\":%lu,\"rejectedPackets\":%lu,"
        "\"reconnects\":%lu,\"lastDisconnectReasonName\":\"%s\","
        "\"eventsDropped\":%lu,\"recoveredStaleCount\":%lu,"
        "\"recoveredStaleMs\":%lu,\"rssi\":%s},"
        "\"lastCommand\":{\"requestId\":%lu,\"state\":\"%s\"},"
        "\"compileFlags\":{\"buzzer\":\"%s\",\"remoteMachineControl\":%s,"
        "\"arch\":\"%s\",\"machineType\":\"%s\",\"stopPulseMs\":%lu,"
        "\"maxSinglePressMs\":%lu,\"development\":%s},"
        "\"serial\":{\"io4\":\"%s\",\"state\":\"%s\"},"
        "\"boot\":{\"complete\":%s,\"degraded\":%s,\"scaleWorker\":%s}",
        stopperStateName(control.state),
        machineRunStateName(control.machineRunState),
        control.relayClosed ? "true" : "false",
        control.machineRunning ? "true" : "false",
        control.machineStartAckPending ? "true" : "false",
        control.machineStopAckPending ? "true" : "false",
        control.machineOrphanRun ? "true" : "false",
        control.reedOn ? "true" : "false",
        control.physicalActivatorOn ? "true" : "false",
        controlSourceName(control.source),
        cupPresenceStateName(control.cupPresenceState),
        control.cupPresent ? "true" : "false",
        network.apActive ? "true" : "false", network.apIp,
        static_cast<unsigned>(network.apClients),
        network.wifiConfigured ? "true" : "false", safeStaSsid,
        staStateName(network.staState), wifiPsLiveName(network.wifiPs),
        rfCoexPreferenceName(network.wifiCoex),
        staChannelJson, network.staIp,
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
        static_cast<unsigned long>(control.loopIntervalGapMs),
        static_cast<unsigned long>(control.loopMaxGapMs),
        static_cast<unsigned long>(control.freeHeapBytes),
        static_cast<unsigned long>(control.minimumFreeHeapBytes),
        static_cast<unsigned long>(control.largestFreeHeapBlockBytes),
        static_cast<unsigned long>(control.psramSizeBytes),
        static_cast<unsigned long>(control.psramFreeBytes),
        static_cast<unsigned long>(control.psramLargestFreeBlockBytes),
        static_cast<unsigned long>(control.bleHostAllocPsramCount),
        static_cast<unsigned long>(control.bleHostAllocFallbackCount),
        static_cast<unsigned long>(control.bleHostHciRxDropped),
        static_cast<unsigned long>(control.bleHostHciTxDropped),
        control.workBufExternal ? "true" : "false",
        control.jsonArenaExternal ? "true" : "false",
        static_cast<unsigned long>(control.allocExternalFallbackCount),
        static_cast<double>(control.hwmon.cpuLoad5s),
        static_cast<double>(control.hwmon.cpuLoad1m),
        static_cast<double>(control.hwmon.cpuLoad5m),
        static_cast<double>(control.hwmon.cpu0Busy),
        static_cast<double>(control.hwmon.cpu1Busy),
        control.hwmon.cpuLoadValid ? "true" : "false",
        static_cast<unsigned long>(control.hwmon.cpuMhz),
        control.hwmon.tempValid ? "true" : "false",
        static_cast<double>(control.hwmon.tempC),
        static_cast<double>(control.hwmon.tempPeakC),
        static_cast<unsigned long>(control.hwmon.ramTotalBytes),
        static_cast<unsigned long>(control.hwmon.ramUsedBytes),
        static_cast<unsigned long>(control.hwmon.ramFreeBytes),
        relaySafetyStateName(control.safetyState),
        relaySafetyFaultName(control.safetyFault),
        control.taskWatchdogReady ? "true" : "false",
        control.externalSafetyPresent ? "true" : "false",
        control.resetRecoveryRequired ? "true" : "false",
        static_cast<unsigned long>(control.resetReasonCode),
        weightStreamStateName(control.weightStreamState),
        weightControlStateName(control.weightControlState),
        static_cast<unsigned long>(control.scalePacketGaps),
        static_cast<unsigned long>(control.scaleRejectedPackets),
        static_cast<unsigned long>(control.scaleReconnects),
        scaleDisconnectReasonName(control.scaleLastDisconnectReason),
        static_cast<unsigned long>(control.scaleEventsDropped),
        static_cast<unsigned long>(control.scaleRecoveredStaleCount),
        static_cast<unsigned long>(control.scaleRecoveredStaleMs),
        scaleRssiJson,
        static_cast<unsigned long>(network.lastCommandRequestId),
        commandResultStateName(network.lastCommandState),
        compiledBuzzerModeId(),
        REMOTE_MACHINE_CONTROL_ENABLED ? "true" : "false",
        FW_BOARD_ARCH_STRING,
        compiledMachineTypeId(),
        static_cast<unsigned long>(COMPILED_STOP_PULSE_MS),
        static_cast<unsigned long>(COMPILED_MAX_SINGLE_PRESS_MS),
        DEVELOPMENT_BUILD ? "true" : "false",
        usbConsoleIo4StateId(control.usbConsoleIo4Closed),
        usbSerialStateId(control.usbSerialEnableSource),
        control.bootComplete ? "true" : "false",
        control.bootDegraded ? "true" : "false",
        control.scaleWorkerReady ? "true" : "false");
    if (ok) ok = statusJsonAppend(&used, ",\"resetHistory\":[");
    for (uint8_t i = 0; ok && i < control.resetHistoryCount; ++i) {
      const ResetHistoryEntry &entry = control.resetHistory[i];
      ok = statusJsonAppend(
          &used, "%s{\"reason\":\"%s\",\"uptimeMs\":%lu}",
          i ? "," : "", safetyResetReasonName(entry.reasonCode),
          static_cast<unsigned long>(entry.uptimeMs));
    }
    if (ok) ok = statusJsonAppend(&used, "]");
    if (ok) {
      const bool bbwEnabled = !control.config.timerOnly;
      const bool scaleUsable =
          control.scaleAvailable &&
          control.weightStreamState == WeightStreamState::FRESH;
      const bool lastShotCupRemoved =
          control.lastShot.valid &&
          control.lastShot.endReason == EndReason::CUP_REMOVED;
      ok = statusJsonAppend(
          &used,
          ",\"guards\":{"
          "\"bbwEnabled\":%s,\"scaleUsable\":%s,\"lastShotCupRemoved\":%s,"
          "\"noScale\":{\"enabled\":%s,\"armed\":%s,\"hold\":%s,"
          "\"mode\":\"%s\",\"cooldownRemainingMs\":%lu,"
          "\"scaleWasAvailable\":%s},"
          "\"atm\":{\"enabled\":%s,\"armed\":%s,\"enforced\":%s,"
          "\"remainingMs\":%lu},"
          "\"slowExtraction\":{\"enabled\":%s,\"extended\":%s,"
          "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f},"
          "\"fastExtraction\":{\"enabled\":%s,\"extended\":%s,"
          "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f,"
          "\"minBbwBrewTimeRemainingMs\":%lu},"
          "\"accidentalTouch\":{\"enabled\":%s,\"holding\":%s,"
          "\"phase\":\"%s\",\"class\":\"%s\",\"pendingCount\":%u},"
          "\"cupProtection\":{\"enabled\":%s,\"stopIfRemoved\":%s,"
          "\"requireCupToStart\":%s,\"present\":%s,\"startHold\":%s,"
          "\"removedPending\":%s,\"bbwProtectionActive\":%s,"
          "\"bbwProtectionEnded\":%s}}",
          bbwEnabled ? "true" : "false", scaleUsable ? "true" : "false",
          lastShotCupRemoved ? "true" : "false",
          control.noScaleShotGuardEnabled ? "true" : "false",
          control.noScaleShotGuardArmed ? "true" : "false",
          control.noScaleShotGuardHold ? "true" : "false",
          noScaleBbwModeId(control.config.noScaleBbwMode),
          static_cast<unsigned long>(
              control.noScaleShotGuardCooldownRemainingMs),
          control.noScaleShotGuardScaleWasAvailable ? "true" : "false",
          control.config.autoToManualGuardEnabled ? "true" : "false",
          control.cycleAutoToManualGuardArmed ? "true" : "false",
          control.cycleAutoToManualGuardEnforced ? "true" : "false",
          static_cast<unsigned long>(
              control.cycleAutoToManualGuardRemainingMs),
          control.config.slowExtractionGuardEnabled ? "true" : "false",
          control.cycleSlowExtractionExtended ? "true" : "false",
          control.cycleTargetReachedEarly ? "true" : "false",
          static_cast<double>(control.cycleActiveStopWeightG),
          control.config.fastExtractionGuardEnabled ? "true" : "false",
          control.cycleExtractionExtended ? "true" : "false",
          control.cycleTargetReachedEarly ? "true" : "false",
          static_cast<double>(control.cycleActiveStopWeightG),
          static_cast<unsigned long>(control.cycleMinBbwBrewTimeRemainingMs),
          control.config.avoidAccidentalTouchEnabled ? "true" : "false",
          control.cycleAccidentalTouchHolding ? "true" : "false",
          accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
              control.cycleAccidentalTouchPhase)),
          accidentalTouchClassName(static_cast<AccidentalTouchClass>(
              control.cycleAccidentalTouchClass)),
          static_cast<unsigned>(control.cycleAccidentalTouchPendingCount),
          control.config.cupProtectionEnabled ? "true" : "false",
          control.config.stopIfCupRemoved ? "true" : "false",
          control.config.requireCupToStart ? "true" : "false",
          control.cupPresent ? "true" : "false",
          control.cupStartGuardHold ? "true" : "false",
          control.cycleCupRemovedPending ? "true" : "false",
          control.cycleBbwProtectionEnabled && !control.cycleBbwProtectionEnded
              ? "true"
              : "false",
          control.cycleBbwProtectionEnded ? "true" : "false");
    }
    if (ok) {
      if (self.callbacks_.copyTaskProfiler != nullptr) {
        self.callbacks_.copyTaskProfiler(self.workBuf_->taskProfiler);
      } else {
        self.workBuf_->taskProfiler = TaskProfilerSnapshot{};
      }
      ok = statusJsonAppendTaskProfiler(&used, self.workBuf_->taskProfiler);
    }
    }
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

namespace {

bool debugExportChunk(httpd_req_t *request, const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return true;
  }
  return sendCopiedChunk(request, text, strlen(text)) == ESP_OK;
}

bool debugExportChunkf(httpd_req_t *request, char *buf, size_t cap,
                       const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, cap, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    return false;
  }
  return debugExportChunk(request, buf);
}

}  // namespace

esp_err_t ShotStopperNetwork::debugExportHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // DEBUG EXPORT MAINTENANCE: extend sections below when adding diagnostically
  // relevant settings / state. Bump DEBUG_EXPORT_SCHEMA_VERSION on material
  // schema changes. Never include secrets.
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  self.loadControlStatus(work.control);
  if (self.callbacks_.copyDebugExportExtras != nullptr) {
    self.callbacks_.copyDebugExportExtras(work.debugExport, work.control);
  } else {
    work.debugExport = DebugExportExtras{};
  }
  const ControlStatusSnapshot &c = work.control;
  const DebugExportExtras &x = work.debugExport;
  const NetworkStatusSnapshot network = self.snapshot();
  const TimeStatusSnapshot timeStatus = g_wallClock.snapshot(millis());
  const bool configMutable = controlAllowsConfiguration(c);
  char *buf = work.statusJson;
  constexpr size_t cap = NetworkWorkBuf::kStatusJson;

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Content-Disposition",
                     "attachment; filename=\"shotstopper-debug.json\"");

  bool ok = debugExportChunkf(
      request, buf, cap,
      "{\"exportSchemaVersion\":%lu,\"exportedAtUtcSec\":%lu,"
      "\"firmwareVersion\":\"%s\",\"bootId\":%lu,\"machineType\":\"%s\","
      "\"boardArch\":\"%s\",\"sections\":{",
      static_cast<unsigned long>(DEBUG_EXPORT_SCHEMA_VERSION),
      static_cast<unsigned long>(timeStatus.utcSec), FW_VERSION,
      static_cast<unsigned long>(c.bootId), compiledMachineTypeId(),
      FW_BOARD_ARCH_STRING);

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"envelope\":{\"configMutable\":%s,\"liveShot\":%s,"
           "\"configRevision\":%lu,\"ringRetainLogLevel\":\"%s\","
           "\"serialDebugOutput\":%s,\"bootComplete\":%s,\"bootDegraded\":%s,"
           "\"scaleWorkerReady\":%s},",
           configMutable ? "true" : "false",
           (c.activeCycle || c.machineRunning || c.relayClosed) ? "true"
                                                                : "false",
           static_cast<unsigned long>(c.config.revision),
           logLevelName(
               static_cast<LogLevel>(c.config.ringRetainLogLevel)),
           c.config.serialDebugOutput ? "true" : "false",
           c.bootComplete ? "true" : "false",
           c.bootDegraded ? "true" : "false",
           c.scaleWorkerReady ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"runtimeConfig\":{\"revision\":%lu,\"goalWeightG\":%u,"
           "\"weightOffsetG\":%.2f,\"weightOffsetBaselineG\":%.2f,"
           "\"autoTare\":%s,\"postTareBaselineGraceMs\":%lu,"
           "\"timerOnly\":%s,\"brewByWeight\":%s,\"canTareStartTimer\":%s,"
           "\"scaleTimerStopExtraDelayMs\":%lu,\"dripDelayMs\":%lu,"
           "\"soundAlertsMuted\":%s,\"firstDropBeep\":%s,"
           "\"paddleReturnReminderBeep\":%s,"
           "\"paddleReturnReminderIntervalMs\":%lu,"
           "\"paddleReturnReminderMaxDurationMs\":%lu,"
           "\"bbwProtectionMs\":%lu,\"operationalWallMs\":%lu,"
           "\"fastExtractionGuardEnabled\":%s,\"maxRecoveryWeightG\":%.1f,"
           "\"minBbwBrewTimeMs\":%lu,\"slowExtractionGuardEnabled\":%s,"
           "\"minRecoveryWeightG\":%.1f,\"maxBbwBrewTimeMs\":%lu,"
           "\"autoToManualGuardEnabled\":%s,"
           "\"autoToManualGuardLimitMode\":\"%s\","
           "\"autoToManualGuardManualLimitMs\":%lu,"
           "\"autoToManualGuardBaselineMs\":%lu,"
           "\"cupProtectionEnabled\":%s,\"stopIfCupRemoved\":%s,"
           "\"requireCupToStart\":%s,\"avoidAccidentalTouchEnabled\":%s,"
           "\"cupPresentWeightG\":%.1f,\"cupRemovedWeightG\":%.1f,"
           "\"avoidBbwShotWithoutScale\":%s,\"noScaleBbwMode\":\"%s\","
           "\"autoRetare\":%s,"
           "\"retareWindowMs\":%lu,\"minimumCupWeightG\":%.1f,"
           "\"lastShotCooldownMs\":%lu,\"timezoneOffsetMinutes\":%d,"
           "\"serialDebugOutput\":%s},",
           static_cast<unsigned long>(c.config.revision),
           static_cast<unsigned>(c.config.goalWeightG),
           static_cast<double>(c.config.weightOffsetG),
           static_cast<double>(c.config.weightOffsetBaselineG),
           c.config.autoTare ? "true" : "false",
           static_cast<unsigned long>(c.config.postTareBaselineGraceMs),
           c.config.timerOnly ? "true" : "false",
           c.config.timerOnly ? "false" : "true",
           c.config.canTareStartTimer ? "true" : "false",
           static_cast<unsigned long>(c.config.scaleTimerStopExtraDelayMs),
           static_cast<unsigned long>(c.config.dripDelayMs),
           c.config.soundAlertsMuted ? "true" : "false",
           c.config.firstDropBeep ? "true" : "false",
           c.config.paddleReturnReminderBeep ? "true" : "false",
           static_cast<unsigned long>(c.config.paddleReturnReminderIntervalMs),
           static_cast<unsigned long>(
               c.config.paddleReturnReminderMaxDurationMs),
           static_cast<unsigned long>(c.config.bbwProtectionMs),
           static_cast<unsigned long>(c.config.operationalWallMs),
           c.config.fastExtractionGuardEnabled ? "true" : "false",
           static_cast<double>(c.config.maxRecoveryWeightG),
           static_cast<unsigned long>(c.config.minBbwBrewTimeMs),
           c.config.slowExtractionGuardEnabled ? "true" : "false",
           static_cast<double>(c.config.minRecoveryWeightG),
           static_cast<unsigned long>(c.config.maxBbwBrewTimeMs),
           c.config.autoToManualGuardEnabled ? "true" : "false",
           autoToManualGuardLimitModeId(c.config.autoToManualGuardLimitMode),
           static_cast<unsigned long>(c.config.autoToManualGuardManualLimitMs),
           static_cast<unsigned long>(c.config.autoToManualGuardBaselineMs),
           c.config.cupProtectionEnabled ? "true" : "false",
           c.config.stopIfCupRemoved ? "true" : "false",
           c.config.requireCupToStart ? "true" : "false",
           c.config.avoidAccidentalTouchEnabled ? "true" : "false",
           static_cast<double>(c.config.cupPresentWeightG),
           static_cast<double>(c.config.cupRemovedWeightG),
           noScaleBbwEnabled(c.config.noScaleBbwMode) ? "true" : "false",
           noScaleBbwModeId(c.config.noScaleBbwMode),
           c.config.autoRetare ? "true" : "false",
           static_cast<unsigned long>(c.config.retareWindowMs),
           static_cast<double>(c.config.minimumCupWeightG),
           static_cast<unsigned long>(c.config.lastShotCooldownMs),
           static_cast<int>(c.config.timezoneOffsetMinutes),
           c.config.serialDebugOutput ? "true" : "false");

  if (self.callbacks_.copyPresetBank != nullptr) {
    self.callbacks_.copyPresetBank(&work.presetBank);
  } else {
    memset(&work.presetBank, 0, sizeof(work.presetBank));
  }
  ok = ok && debugExportChunk(request, "\"presets\":{\"activeId\":");
  ok = ok &&
       debugExportChunkf(request, buf, cap, "%u,\"items\":[",
                         static_cast<unsigned>(work.presetBank.activeId));
  for (uint8_t i = 0;
       ok && i < work.presetBank.count && i < MAX_SHOT_PRESETS; ++i) {
    const ShotPreset &p = work.presetBank.presets[i];
    char safeName[SHOT_PRESET_NAME_CAPACITY] = {};
    sanitizeJsonEmbed(p.name, safeName, sizeof(safeName));
    ok = debugExportChunkf(
        request, buf, cap,
        "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s,\"brewByWeight\":%s,"
        "\"goalWeightG\":%u,\"minBbwBrewTimeMs\":%lu,\"maxBbwBrewTimeMs\":%lu,"
        "\"maxRecoveryWeightG\":%.1f,\"minRecoveryWeightG\":%.1f,"
        "\"fastExtractionGuardEnabled\":%s,\"slowExtractionGuardEnabled\":%s,"
        "\"autoToManualGuardEnabled\":%s,\"autoToManualGuardSamplesDs\":[%u,%u,"
        "%u,%u,%u]}",
        i == 0 ? "" : ",", static_cast<unsigned>(p.id), safeName,
        p.isFactory ? "true" : "false", p.brewByWeight ? "true" : "false",
        static_cast<unsigned>(p.goalWeightG),
        static_cast<unsigned long>(p.minBbwBrewTimeMs),
        static_cast<unsigned long>(p.maxBbwBrewTimeMs),
        static_cast<double>(p.maxRecoveryWeightG),
        static_cast<double>(p.minRecoveryWeightG),
        p.fastExtractionGuardEnabled ? "true" : "false",
        p.slowExtractionGuardEnabled ? "true" : "false",
        p.autoToManualGuardEnabled ? "true" : "false",
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[0]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[1]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[2]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[3]),
        static_cast<unsigned>(p.autoToManualGuardSamplesDs[4]));
  }
  ok = ok && debugExportChunk(request, "]},");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"controlSnapshot\":{\"state\":\"%s\",\"activeCycle\":%s,"
           "\"relayClosed\":%s,\"machineRunning\":%s,\"reedOn\":%s,"
           "\"physicalActivatorOn\":%s,\"controlSource\":\"%s\","
           "\"weightStreamState\":\"%s\",\"weightControlState\":\"%s\","
           "\"scaleAvailable\":%s,\"currentWeightValid\":%s,"
           "\"currentWeightG\":%.2f,\"observedWeightValid\":%s,"
           "\"observedWeightG\":%.2f,\"cycleId\":%lu,\"uptimeMs\":%lu,"
           "\"loopIntervalGapMs\":%lu,\"loopMaxGapMs\":%lu,"
           "\"freeHeapBytes\":%lu,\"minimumFreeHeapBytes\":%lu,"
           "\"noScaleShotGuardEnabled\":%s,\"noScaleShotGuardArmed\":%s,"
           "\"noScaleShotGuardHold\":%s,\"cupStartGuardHold\":%s,"
           "\"cycleExtractionExtended\":%s,\"cycleSlowExtractionExtended\":%s,"
           "\"cycleTargetReachedEarly\":%s,\"cycleAutoToManualGuardArmed\":%s,"
           "\"cycleAutoToManualGuardEnforced\":%s,"
           "\"cycleAccidentalTouchHolding\":%s,"
           "\"cycleAccidentalTouchPhase\":\"%s\","
           "\"cycleAccidentalTouchClass\":\"%s\","
           "\"cycleCupRemovedPending\":%s,\"cycleBbwProtectionEnabled\":%s,"
           "\"cycleBbwProtectionEnded\":%s},",
           stopperStateName(c.state), c.activeCycle ? "true" : "false",
           c.relayClosed ? "true" : "false",
           c.machineRunning ? "true" : "false", c.reedOn ? "true" : "false",
           c.physicalActivatorOn ? "true" : "false",
           controlSourceName(c.source),
           weightStreamStateName(c.weightStreamState),
           weightControlStateName(c.weightControlState),
           c.scaleAvailable ? "true" : "false",
           c.currentWeightValid ? "true" : "false",
           static_cast<double>(c.currentWeightG),
           c.observedWeightValid ? "true" : "false",
           static_cast<double>(c.observedWeightG),
           static_cast<unsigned long>(c.cycleId),
           static_cast<unsigned long>(c.uptimeMs),
           static_cast<unsigned long>(c.loopIntervalGapMs),
           static_cast<unsigned long>(c.loopMaxGapMs),
           static_cast<unsigned long>(c.freeHeapBytes),
           static_cast<unsigned long>(c.minimumFreeHeapBytes),
           c.noScaleShotGuardEnabled ? "true" : "false",
           c.noScaleShotGuardArmed ? "true" : "false",
           c.noScaleShotGuardHold ? "true" : "false",
           c.cupStartGuardHold ? "true" : "false",
           c.cycleExtractionExtended ? "true" : "false",
           c.cycleSlowExtractionExtended ? "true" : "false",
           c.cycleTargetReachedEarly ? "true" : "false",
           c.cycleAutoToManualGuardArmed ? "true" : "false",
           c.cycleAutoToManualGuardEnforced ? "true" : "false",
           c.cycleAccidentalTouchHolding ? "true" : "false",
           accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
               c.cycleAccidentalTouchPhase)),
           accidentalTouchClassName(static_cast<AccidentalTouchClass>(
               c.cycleAccidentalTouchClass)),
           c.cycleCupRemovedPending ? "true" : "false",
           c.cycleBbwProtectionEnabled ? "true" : "false",
           c.cycleBbwProtectionEnded ? "true" : "false");

  if (x.sessionActive) {
    ok = ok &&
         debugExportChunkf(
             request, buf, cap,
             "\"session\":{\"active\":true,\"automaticEnabled\":%s,"
             "\"bbwProtectionEnabled\":%s,\"bbwProtectionEnded\":%s,"
             "\"startedWithScale\":%s,\"scaleWasLost\":%s,"
             "\"cupRemovedPending\":%s,\"extractionExtended\":%s,"
             "\"slowExtractionExtended\":%s,\"targetReachedEarly\":%s,"
             "\"autoToManualGuardArmed\":%s,\"autoToManualGuardEnforced\":%s,"
             "\"accidentalTouchHolding\":%s,\"accidentalTouchPhase\":\"%s\","
             "\"accidentalTouchClass\":\"%s\",\"accidentalTouchPendingCount\":%u,"
             "\"activePresetId\":%u,\"weightControlState\":\"%s\","
             "\"id\":%lu,\"startedAtMs\":%lu,\"firstDropMs\":%lu,"
             "\"autoToManualGuardDeadlineAtMs\":%lu,\"targetReachedAtMs\":%lu},",
             x.sessionAutomaticEnabled ? "true" : "false",
             x.sessionBbwProtectionEnabled ? "true" : "false",
             x.sessionBbwProtectionEnded ? "true" : "false",
             x.sessionStartedWithScale ? "true" : "false",
             x.sessionScaleWasLost ? "true" : "false",
             x.sessionCupRemovedPending ? "true" : "false",
             x.sessionExtractionExtended ? "true" : "false",
             x.sessionSlowExtractionExtended ? "true" : "false",
             x.sessionTargetReachedEarly ? "true" : "false",
             x.sessionAutoToManualGuardArmed ? "true" : "false",
             x.sessionAutoToManualGuardEnforced ? "true" : "false",
             x.sessionAccidentalTouchHolding ? "true" : "false",
             accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
                 x.sessionAccidentalTouchPhase)),
             accidentalTouchClassName(static_cast<AccidentalTouchClass>(
                 x.sessionAccidentalTouchClass)),
             static_cast<unsigned>(x.sessionAccidentalTouchPendingCount),
             static_cast<unsigned>(x.sessionActivePresetId),
             weightControlStateName(static_cast<WeightControlState>(
                 x.sessionWeightControlState)),
             static_cast<unsigned long>(x.sessionId),
             static_cast<unsigned long>(x.sessionStartedAtMs),
             static_cast<unsigned long>(x.sessionFirstDropMs),
             static_cast<unsigned long>(x.sessionAutoToManualGuardDeadlineAtMs),
             static_cast<unsigned long>(x.sessionTargetReachedAtMs));
  } else {
    ok = ok && debugExportChunk(request, "\"session\":null,");
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"relay\":{\"state\":\"%s\",\"fault\":\"%s\",\"closed\":%s,"
           "\"commandedClosed\":%s,\"feedbackClosed\":%s,"
           "\"feedbackAvailable\":%s,\"externalSafetyPresent\":%s,"
           "\"watchdogReady\":%s,\"timersReady\":%s,\"tripped\":%s,"
           "\"operationalTripped\":%s,\"generation\":%lu,\"closedAtMs\":%lu,"
           "\"operationalLimitMs\":%lu,\"resetReasonCode\":%lu,"
           "\"unsafeResetCount\":%lu,\"resetRecoveryRequired\":%s,"
           "\"bootLoopDetected\":%s,\"resetHistory\":[",
           relaySafetyStateName(x.relay.state),
           relaySafetyFaultName(x.relay.fault),
           x.relay.closed ? "true" : "false",
           x.relay.commandedClosed ? "true" : "false",
           x.relay.feedbackClosed ? "true" : "false",
           x.relay.feedbackAvailable ? "true" : "false",
           x.relay.externalSafetyPresent ? "true" : "false",
           x.relay.watchdogReady ? "true" : "false",
           x.relay.timersReady ? "true" : "false",
           x.relay.tripped ? "true" : "false",
           x.relay.operationalTripped ? "true" : "false",
           static_cast<unsigned long>(x.relay.generation),
           static_cast<unsigned long>(x.relay.closedAtMs),
           static_cast<unsigned long>(x.relay.operationalLimitMs),
           static_cast<unsigned long>(x.relay.resetReasonCode),
           static_cast<unsigned long>(x.relay.unsafeResetCount),
           x.relay.resetRecoveryRequired ? "true" : "false",
           x.relay.bootLoopDetected ? "true" : "false");
  for (uint8_t i = 0; i < x.relay.resetHistoryCount; ++i) {
    const ResetHistoryEntry &entry = x.relay.resetHistory[i];
    ok = ok && debugExportChunkf(
                   request, buf, cap,
                   "%s{\"reason\":\"%s\",\"uptimeMs\":%lu}",
                   i ? "," : "", safetyResetReasonName(entry.reasonCode),
                   static_cast<unsigned long>(entry.uptimeMs));
  }
  ok = ok && debugExportChunk(request, "]},");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"machine\":{\"rawActivatorOn\":%s,\"physicalActivatorOn\":%s,"
           "\"machineRunState\":\"%s\",\"machineStartAckPending\":%s,"
           "\"machineStopAckPending\":%s,\"machineOrphanRun\":%s},",
           x.rawActivatorOn ? "true" : "false",
           x.physicalActivatorOn ? "true" : "false",
           machineRunStateName(static_cast<MachineRunState>(x.machineRunState)),
           x.machineStartAckPending ? "true" : "false",
           x.machineStopAckPending ? "true" : "false",
           x.machineOrphanRun ? "true" : "false");

  {
    const bool bbwEnabled = !c.config.timerOnly;
    const bool scaleUsable =
        c.scaleAvailable && c.weightStreamState == WeightStreamState::FRESH;
    const bool lastShotCupRemoved =
        c.lastShot.valid && c.lastShot.endReason == EndReason::CUP_REMOVED;
    ok = ok &&
         debugExportChunkf(
             request, buf, cap,
             "\"guards\":{\"bbwEnabled\":%s,\"scaleUsable\":%s,"
             "\"lastShotCupRemoved\":%s,"
             "\"noScale\":{\"enabled\":%s,\"armed\":%s,\"hold\":%s,"
             "\"mode\":\"%s\",\"cooldownRemainingMs\":%lu,"
             "\"scaleWasAvailable\":%s},"
             "\"atm\":{\"enabled\":%s,\"armed\":%s,\"enforced\":%s,"
             "\"remainingMs\":%lu},"
             "\"slowExtraction\":{\"enabled\":%s,\"extended\":%s,"
             "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f},"
             "\"fastExtraction\":{\"enabled\":%s,\"extended\":%s,"
             "\"targetReachedEarly\":%s,\"activeStopWeightG\":%.1f,"
             "\"minBbwBrewTimeRemainingMs\":%lu},"
             "\"accidentalTouch\":{\"enabled\":%s,\"holding\":%s,"
             "\"phase\":\"%s\",\"class\":\"%s\",\"pendingCount\":%u},"
             "\"cupProtection\":{\"enabled\":%s,\"stopIfRemoved\":%s,"
             "\"requireCupToStart\":%s,\"present\":%s,\"startHold\":%s,"
             "\"removedPending\":%s,\"bbwProtectionActive\":%s,"
             "\"bbwProtectionEnded\":%s}},",
             bbwEnabled ? "true" : "false", scaleUsable ? "true" : "false",
             lastShotCupRemoved ? "true" : "false",
             c.noScaleShotGuardEnabled ? "true" : "false",
             c.noScaleShotGuardArmed ? "true" : "false",
             c.noScaleShotGuardHold ? "true" : "false",
             noScaleBbwModeId(c.config.noScaleBbwMode),
             static_cast<unsigned long>(
                 c.noScaleShotGuardCooldownRemainingMs),
             c.noScaleShotGuardScaleWasAvailable ? "true" : "false",
             c.config.autoToManualGuardEnabled ? "true" : "false",
             c.cycleAutoToManualGuardArmed ? "true" : "false",
             c.cycleAutoToManualGuardEnforced ? "true" : "false",
             static_cast<unsigned long>(c.cycleAutoToManualGuardRemainingMs),
             c.config.slowExtractionGuardEnabled ? "true" : "false",
             c.cycleSlowExtractionExtended ? "true" : "false",
             c.cycleTargetReachedEarly ? "true" : "false",
             static_cast<double>(c.cycleActiveStopWeightG),
             c.config.fastExtractionGuardEnabled ? "true" : "false",
             c.cycleExtractionExtended ? "true" : "false",
             c.cycleTargetReachedEarly ? "true" : "false",
             static_cast<double>(c.cycleActiveStopWeightG),
             static_cast<unsigned long>(c.cycleMinBbwBrewTimeRemainingMs),
             c.config.avoidAccidentalTouchEnabled ? "true" : "false",
             c.cycleAccidentalTouchHolding ? "true" : "false",
             accidentalTouchPhaseName(static_cast<AccidentalTouchPhase>(
                 c.cycleAccidentalTouchPhase)),
             accidentalTouchClassName(static_cast<AccidentalTouchClass>(
                 c.cycleAccidentalTouchClass)),
             static_cast<unsigned>(c.cycleAccidentalTouchPendingCount),
             c.config.cupProtectionEnabled ? "true" : "false",
             c.config.stopIfCupRemoved ? "true" : "false",
             c.config.requireCupToStart ? "true" : "false",
             c.cupPresent ? "true" : "false",
             c.cupStartGuardHold ? "true" : "false",
             c.cycleCupRemovedPending ? "true" : "false",
             c.cycleBbwProtectionEnabled && !c.cycleBbwProtectionEnded ? "true"
                                                                       : "false",
             c.cycleBbwProtectionEnded ? "true" : "false");
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"cupPresence\":{\"state\":\"%s\",\"holdTransitions\":%s,"
           "\"inNegativeHole\":%s,\"removedArmed\":%s,"
           "\"removedConfirmations\":%u,\"placeStabilitySamples\":%u,"
           "\"holeWeightG\":%.2f,\"placeCandidateWeightG\":%.2f},",
           cupPresenceStateName(static_cast<CupPresenceState>(x.cupState)),
           x.cupHoldTransitions ? "true" : "false",
           x.cupInNegativeHole ? "true" : "false",
           x.cupRemovedArmed ? "true" : "false",
           static_cast<unsigned>(x.cupRemovedConfirmations),
           static_cast<unsigned>(x.cupPlaceStabilitySamples),
           static_cast<double>(x.cupHoleWeightG),
           static_cast<double>(x.cupPlaceCandidateWeightG));

  char scaleLinkRssiJson[16] = "null";
  if (c.scaleRssiValid) {
    snprintf(scaleLinkRssiJson, sizeof(scaleLinkRssiJson), "%d",
             static_cast<int>(c.scaleRssi));
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"scaleLink\":{\"state\":\"%s\",\"disconnectSequence\":%lu,"
           "\"connectionGeneration\":%lu,\"packetSequence\":%lu,"
           "\"packetGaps\":%lu,\"rejectedPackets\":%lu,\"reconnects\":%lu,"
           "\"recoveredStaleCount\":%lu,\"recoveredStaleMs\":%lu,"
           "\"workerProgressAtMs\":%lu,\"timerValid\":%s,\"timerMs\":%lu,"
           "\"timerAgeMs\":%lu,\"protocol\":\"%s\","
           "\"lastDisconnectReasonName\":\"%s\",\"rssi\":%s},",
           debugExportScaleLinkStateName(x.scaleLinkState),
           static_cast<unsigned long>(x.scaleDisconnectSequence),
           static_cast<unsigned long>(x.scaleConnectionGeneration),
           static_cast<unsigned long>(x.scalePacketSequence),
           static_cast<unsigned long>(c.scalePacketGaps),
           static_cast<unsigned long>(c.scaleRejectedPackets),
           static_cast<unsigned long>(c.scaleReconnects),
           static_cast<unsigned long>(c.scaleRecoveredStaleCount),
           static_cast<unsigned long>(c.scaleRecoveredStaleMs),
           static_cast<unsigned long>(x.scaleWorkerProgressAtMs),
           x.scaleTimerValid ? "true" : "false",
           static_cast<unsigned long>(x.scaleTimerMs),
           static_cast<unsigned long>(x.scaleTimerAgeMs), x.scaleProtocolName,
           scaleDisconnectReasonName(c.scaleLastDisconnectReason),
           scaleLinkRssiJson);

  char safeStaSsid[WIFI_SSID_CAPACITY] = {};
  sanitizeJsonEmbed(network.staSsid, safeStaSsid, sizeof(safeStaSsid));
  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"network\":{\"networkActive\":%s,\"apActive\":%s,"
           "\"wifiConfigured\":%s,\"staState\":\"%s\",\"staIp\":\"%s\","
           "\"ssid\":\"%s\",\"apIp\":\"%s\",\"apClients\":%u,"
           "\"httpActive\":%s,\"staMac\":\"%s\",\"staBssid\":\"%s\","
           "\"apMac\":\"%s\",\"staReconnectHeld\":%s,\"apStartHeld\":%s,"
           "\"httpStartHeld\":%s,\"taskAgeMs\":%lu,\"taskStackMinWords\":%lu,"
           "\"startupFailures\":%lu,\"channel\":%u,\"rssi\":%d,"
           "\"signalQualityPct\":%u,\"configState\":\"%s\","
           "\"ipMode\":\"%s\",\"confirmRemainingMs\":%lu},",
           network.networkActive ? "true" : "false",
           network.apActive ? "true" : "false",
           network.wifiConfigured ? "true" : "false",
           staStateName(network.staState), network.staIp, safeStaSsid,
           network.apIp, static_cast<unsigned>(network.apClients),
           network.httpActive ? "true" : "false", network.staMac,
           network.staBssid, network.apMac,
           network.staReconnectHeld ? "true" : "false",
           network.apStartHeld ? "true" : "false",
           network.httpStartHeld ? "true" : "false",
           static_cast<unsigned long>(network.taskAgeMs),
           static_cast<unsigned long>(network.taskStackMinWords),
           static_cast<unsigned long>(network.startupFailures),
           static_cast<unsigned>(network.channel),
           static_cast<int>(network.staRssi),
           static_cast<unsigned>(network.staSignalQualityPct),
           staConfigStateName(network.staConfigState),
           staIpModeName(network.staIpMode),
           static_cast<unsigned long>(network.confirmRemainingMs));

  char safeActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
  sanitizeJsonEmbed(timeStatus.activeServer, safeActiveServer,
                    sizeof(safeActiveServer));
  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"time\":{\"state\":\"%s\",\"utcSec\":%lu,\"lastSyncAgeMs\":%lu,"
           "\"nextRetryInMs\":%lu,\"activeServer\":\"%s\"},",
           timeSyncStateName(timeStatus.state),
           static_cast<unsigned long>(timeStatus.utcSec),
           static_cast<unsigned long>(timeStatus.lastSyncAgeMs),
           static_cast<unsigned long>(timeStatus.nextRetryInMs),
           safeActiveServer);

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"health\":{\"uptimeMs\":%lu,\"loopIntervalGapMs\":%lu,"
           "\"loopMaxGapMs\":%lu,\"loopStackMinWords\":%lu,"
           "\"scaleStackMinWords\":%lu,\"freeHeapBytes\":%lu,"
           "\"minimumFreeHeapBytes\":%lu,\"largestFreeHeapBlockBytes\":%lu,"
           "\"psramSizeBytes\":%lu,\"psramFreeBytes\":%lu,"
           "\"psramLargestFreeBlockBytes\":%lu,\"bleHostAllocPsram\":%lu,"
           "\"bleHostAllocFallback\":%lu,\"hciRxDropped\":%lu,"
           "\"hciTxDropped\":%lu,\"workBufExternal\":%s,"
           "\"jsonArenaExternal\":%s,\"allocExternalFallback\":%lu,"
           "\"heapAlertLatched\":%s,"
           "\"stackAlertLatched\":%s,\"loopGapAlertLatched\":%s,"
           "\"cpuLoad5s\":%.2f,\"cpuLoad1m\":%.2f,\"cpuLoad5m\":%.2f,"
           "\"cpuMhz\":%lu,\"tempC\":%.1f,\"tempPeakC\":%.1f},",
           static_cast<unsigned long>(c.uptimeMs),
           static_cast<unsigned long>(c.loopIntervalGapMs),
           static_cast<unsigned long>(c.loopMaxGapMs),
           static_cast<unsigned long>(c.loopStackMinWords),
           static_cast<unsigned long>(c.scaleStackMinWords),
           static_cast<unsigned long>(c.freeHeapBytes),
           static_cast<unsigned long>(c.minimumFreeHeapBytes),
           static_cast<unsigned long>(c.largestFreeHeapBlockBytes),
           static_cast<unsigned long>(c.psramSizeBytes),
           static_cast<unsigned long>(c.psramFreeBytes),
           static_cast<unsigned long>(c.psramLargestFreeBlockBytes),
           static_cast<unsigned long>(c.bleHostAllocPsramCount),
           static_cast<unsigned long>(c.bleHostAllocFallbackCount),
           static_cast<unsigned long>(c.bleHostHciRxDropped),
           static_cast<unsigned long>(c.bleHostHciTxDropped),
           c.workBufExternal ? "true" : "false",
           c.jsonArenaExternal ? "true" : "false",
           static_cast<unsigned long>(c.allocExternalFallbackCount),
           x.healthHeapAlertLatched ? "true" : "false",
           x.healthStackAlertLatched ? "true" : "false",
           x.healthLoopGapAlertLatched ? "true" : "false",
           static_cast<double>(c.hwmon.cpuLoad5s),
           static_cast<double>(c.hwmon.cpuLoad1m),
           static_cast<double>(c.hwmon.cpuLoad5m),
           static_cast<unsigned long>(c.hwmon.cpuMhz),
           static_cast<double>(c.hwmon.tempC),
           static_cast<double>(c.hwmon.tempPeakC));

  if (self.callbacks_.copyTaskProfiler != nullptr) {
    self.callbacks_.copyTaskProfiler(work.taskProfiler);
  } else {
    work.taskProfiler = TaskProfilerSnapshot{};
  }
  {
    size_t used = 0;
    buf[0] = '\0';
    ok = ok && jsonScratchAppend(buf, cap, &used, "\"tasks\":");
    ok = ok &&
         formatTaskProfilerObject(buf, cap, &used, work.taskProfiler);
    ok = ok && jsonScratchAppend(buf, cap, &used, ",");
    ok = ok && debugExportChunk(request, buf);
  }

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"safety\":{\"state\":\"%s\",\"fault\":\"%s\","
           "\"taskWatchdogReady\":%s,\"externalHardware\":%s,"
           "\"recoveryRequired\":%s,\"resetReasonCode\":%lu,"
           "\"unsafeResetCount\":%lu,\"bootLoopDetected\":%s,"
           "\"safetyGeneration\":%lu,\"safetyTimersReady\":%s},",
           relaySafetyStateName(c.safetyState),
           relaySafetyFaultName(c.safetyFault),
           c.taskWatchdogReady ? "true" : "false",
           c.externalSafetyPresent ? "true" : "false",
           c.resetRecoveryRequired ? "true" : "false",
           static_cast<unsigned long>(c.resetReasonCode),
           static_cast<unsigned long>(c.unsafeResetCount),
           c.bootLoopDetected ? "true" : "false",
           static_cast<unsigned long>(c.safetyGeneration),
           c.safetyTimersReady ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"bleCompanion\":{\"enabled\":%s,\"active\":%s,"
           "\"restartRequired\":%s,\"stackReady\":%s,\"advertising\":%s,"
           "\"connected\":%s,\"protocolVersion\":%u,\"acceptedWrites\":%lu,"
           "\"rejectedWrites\":%lu,\"scanIntensity\":\"%s\"},",
           c.bleCompanionEnabled ? "true" : "false",
           c.bleCompanionActive ? "true" : "false",
           c.bleCompanionRestartRequired ? "true" : "false",
           c.bleCompanionStackReady ? "true" : "false",
           c.bleCompanionAdvertising ? "true" : "false",
           c.bleCompanionConnected ? "true" : "false",
           static_cast<unsigned>(c.bleCompanionProtocolVersion),
           static_cast<unsigned long>(c.bleCompanionAcceptedWrites),
           static_cast<unsigned long>(c.bleCompanionRejectedWrites),
           bleScanIntensityName(clampBleScanIntensity(
               c.bleCompanionScanIntensity)));

  self.buildOtaJson(work.otaJson, NetworkWorkBuf::kOtaJson, controlGateOf(c));
  ok = ok && debugExportChunk(request, "\"ota\":");
  ok = ok && debugExportChunk(request, work.otaJson);
  ok = ok && debugExportChunk(request, ",");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"lastShot\":{\"valid\":%s,\"goalWeightG\":%u,\"durationMs\":%lu,"
           "\"endReason\":\"%s\",\"shotType\":\"%s\","
           "\"extractionExtended\":%s,\"slowExtractionExtended\":%s},"
           "\"compileFlags\":{\"buzzer\":\"%s\",\"remoteMachineControl\":%s,"
           "\"arch\":\"%s\",\"machineType\":\"%s\",\"stopPulseMs\":%lu,"
           "\"maxSinglePressMs\":%lu,\"development\":%s},",
           c.lastShot.valid ? "true" : "false",
           static_cast<unsigned>(c.lastShot.goalWeightG),
           static_cast<unsigned long>(c.lastShot.durationMs),
           self.endReasonName(c.lastShot.endReason),
           lastShotTypeName(static_cast<LastShotType>(c.lastShot.shotType)),
           c.lastShot.extractionExtended ? "true" : "false",
           c.lastShot.slowExtractionExtended ? "true" : "false",
           compiledBuzzerModeId(),
           REMOTE_MACHINE_CONTROL_ENABLED ? "true" : "false",
           FW_BOARD_ARCH_STRING, compiledMachineTypeId(),
           static_cast<unsigned long>(COMPILED_STOP_PULSE_MS),
           static_cast<unsigned long>(COMPILED_MAX_SINGLE_PRESS_MS),
           DEVELOPMENT_BUILD ? "true" : "false");

  ok = ok &&
       debugExportChunkf(
           request, buf, cap,
           "\"events\":{\"dropped\":%lu,\"items\":[",
           static_cast<unsigned long>(c.debugEventsDropped));
  if (ok && self.callbacks_.copyDebugEvents != nullptr) {
    uint32_t after = 0;
    bool first = true;
    for (;;) {
      const size_t count = self.callbacks_.copyDebugEvents(
          after, work.logBatch, kNetworkLogBatchSize);
      if (count == 0) {
        break;
      }
      for (size_t index = 0; ok && index < count; ++index) {
        const DebugEvent &event = work.logBatch[index];
        ok = debugExportChunkf(
            request, buf, cap,
            "%s{\"sequence\":%lu,\"atMs\":%lu,\"wallSec\":%lu,"
            "\"level\":\"%s\",\"category\":\"%s\",\"code\":\"%s\","
            "\"argument1\":%ld,\"argument2\":%ld}",
            first ? "" : ",", static_cast<unsigned long>(event.sequence),
            static_cast<unsigned long>(event.atMs),
            static_cast<unsigned long>(event.wallSec),
            logLevelName(event.level), debugCategoryName(event.category),
            debugCodeName(event.code), static_cast<long>(event.argument1),
            static_cast<long>(event.argument2));
        first = false;
        after = event.sequence;
      }
      if (count < kNetworkLogBatchSize) {
        break;
      }
    }
  }
  ok = ok && debugExportChunk(request, "]},");

  size_t shotTotal = 0;
  if (self.callbacks_.copyShotRecords != nullptr) {
    shotTotal =
        self.callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY);
  }
  const size_t recent =
      shotTotal < DEBUG_EXPORT_SHOT_SUMMARY_LIMIT
          ? shotTotal
          : DEBUG_EXPORT_SHOT_SUMMARY_LIMIT;
  ok = ok &&
       debugExportChunkf(request, buf, cap,
                         "\"shotLogSummary\":{\"total\":%lu,\"recent\":[",
                         static_cast<unsigned long>(shotTotal));
  for (size_t index = 0; ok && index < recent; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    ok = debugExportChunkf(
        request, buf, cap,
        "%s{\"id\":%lu,\"bootId\":%lu,\"durationDs\":%u,\"goalWeightG\":%u,"
        "\"stopDetail\":\"%s\"}",
        index == 0 ? "" : ",", static_cast<unsigned long>(record.id),
        static_cast<unsigned long>(record.bootId),
        static_cast<unsigned>(record.durationDs),
        static_cast<unsigned>(record.goalWeightG),
        shotLogStopDetailName(
            static_cast<ShotLogStopDetail>(record.stopDetail)));
  }
  ok = ok && debugExportChunk(request, "]}}}");

  if (!ok) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  const esp_err_t finished = httpd_resp_send_chunk(request, nullptr, 0);
  self.unlockWorkBuf();
  return finished;
}

esp_err_t ShotStopperNetwork::logHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // Bounded diagnostic log: fixed enum-derived messages and numeric
  // arguments; credentials and request payloads are never included.
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

  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  const size_t count = self.callbacks_.copyDebugEvents(
      after, work.logBatch, LOG_BATCH_SIZE);
  self.loadControlStatus(work.control);
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
      snprintf(message, sizeof(message), "Advanced Shot Stopper %s (bootId=%ld)",
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
                   static_cast<int32_t>(WebCommandType::REMOTE_ON) &&
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
      copyCString(message, sizeof(message), debugCodeName(event.code));
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             "%s{\"sequence\":%lu,\"atMs\":%lu,\"wallSec\":%lu,"
             "\"level\":\"%s\",\"category\":\"%s\",\"code\":%u,"
             "\"message\":",
             index == 0 ? "" : ",",
             static_cast<unsigned long>(event.sequence),
             static_cast<unsigned long>(event.atMs),
             static_cast<unsigned long>(event.wallSec),
             logLevelName(event.level), debugCategoryName(event.category),
             static_cast<unsigned>(event.code));
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
        ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
    if (sendJsonStringChunk(request, message) != ESP_OK) {
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
    snprintf(work.jsonItem, NetworkWorkBuf::kJsonItem,
             ",\"argument1\":%ld,\"argument2\":%ld}",
             static_cast<long>(event.argument1),
             static_cast<long>(event.argument2));
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
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
  size_t offset = 0;
  size_t limit = SHOT_LOG_PAGE_DEFAULT;
  ShotLogSort sort = ShotLogSort::Date;
  ShotLogSortDir dir = ShotLogSortDir::Desc;
  parseShotsPageQuery(request, offset, limit, sort, dir);
  if (!self.lockWorkBufForStatus()) {
    return self.workBufBusy(request);
  }
  NetworkWorkBuf &work = *self.workBuf_;
  self.loadControlStatus(work.control);
  const size_t count =
      self.callbacks_.copyShotRecords != nullptr
          ? self.callbacks_.copyShotRecords(work.shotRecords, SHOT_LOG_CAPACITY)
          : 0;
  const size_t curveCount =
      self.callbacks_.copyShotCurves != nullptr
          ? self.callbacks_.copyShotCurves(work.shotCurves, SHOT_CURVE_CAPACITY)
          : 0;
  shotLogSortRecords(work.shotRecords, count, sort, dir);
  size_t start = 0;
  size_t pageCount = 0;
  const bool hasMore =
      shotLogPageSlice(count, offset, limit, start, pageCount);

  httpd_resp_set_type(request, JSON_CONTENT_TYPE);
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  char header[160] = {};
  snprintf(header, sizeof(header),
           "{\"bootId\":%lu,\"total\":%u,\"offset\":%u,\"limit\":%u,"
           "\"hasMore\":%s,\"shots\":[",
           static_cast<unsigned long>(work.control.bootId),
           static_cast<unsigned>(count), static_cast<unsigned>(start),
           static_cast<unsigned>(limit), hasMore ? "true" : "false");
  if (httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }

  for (size_t index = start; index < start + pageCount; ++index) {
    const ShotLogRecord &record = work.shotRecords[index];
    char actual[16] = "null";
    char errorG[16] = "null";
    char errorPct[16] = "null";
    char flow[16] = "null";
    char firstDrop[16] = "null";
    char maxRecovery[16] = "null";
    char minBbwBrewTime[16] = "null";
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
    if (record.minBbwBrewTimeDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(minBbwBrewTime, sizeof(minBbwBrewTime), "%.1f",
               static_cast<double>(record.minBbwBrewTimeDs) / 10.0);
    }
    if (record.targetReachedEarlyDs != SHOT_LOG_METRIC_MISSING) {
      snprintf(targetEarly, sizeof(targetEarly), "%.1f",
               static_cast<double>(record.targetReachedEarlyDs) / 10.0);
    }
    const ShotCurveRecord emptyCurve = emptyShotCurveRecord();
    const ShotCurveRecord *curve = findShotCurveById(
        work.shotCurves, curveCount, record.id);
    if (curve == nullptr) {
      curve = &emptyCurve;
    }
    char curveJson[640] = {};
    if (!formatShotCurveJsonBody(curveJson, sizeof(curveJson), *curve)) {
      curveJson[0] = '\0';
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
             "\"minBbwBrewTimeS\":%s,\"targetReachedEarlyS\":%s,"
             "\"actualWeightSource\":\"%s\",\"rating\":%u,%s}",
             index == start ? "" : ",",
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
             maxRecovery, minBbwBrewTime, targetEarly,
             actualWeightSourceName(
                 static_cast<ActualWeightSource>(record.actualWeightSource)),
             static_cast<unsigned>(shotLogRating(record.extractionGuardEnabled)),
             curveJson);
    if (sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem)) !=
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
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before clearing shot history.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_SHOT_LOG") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before clearing the last shot.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "CLEAR_LAST_SHOT") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before deleting shot records.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  uint32_t shotId = 0;
  static const char *const fields[] = {"id"};
  const bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonUint32(root, "id", shotId) && shotId != 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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

esp_err_t ShotStopperNetwork::shotsRateHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  const ControlGateSnapshot status = self.controlGate();
  if (!self.historyMutationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before rating shots.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"id", "lastShot", "rating"};
  uint8_t rating = 0;
  uint32_t shotId = 0;
  bool lastShot = false;
  const bool hasId = jsonFieldPresent(root, "id");
  const bool hasLastShot = jsonFieldPresent(root, "lastShot");
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 3) &&
      jsonUint8(root, "rating", rating) && rating <= SHOT_LOG_RATING_MAX &&
      hasId != hasLastShot &&
      (!hasId || (jsonUint32(root, "id", shotId) && shotId != 0)) &&
      (!hasLastShot || (jsonBoolean(root, "lastShot", lastShot) && lastShot));
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "A rating 0-5 and either a shot id or lastShot is required.");
  }

  if (hasLastShot) {
    if (self.callbacks_.rateLastShot == nullptr ||
        !self.callbacks_.rateLastShot(rating)) {
      return sendError(request, STATUS_NOT_FOUND, "LAST_SHOT_NOT_FOUND",
                       "There is no last shot to rate.");
    }
  } else if (self.callbacks_.rateShotRecord == nullptr ||
             !self.callbacks_.rateShotRecord(shotId, rating)) {
    return sendError(request, STATUS_NOT_FOUND, "SHOT_NOT_FOUND",
                     "The requested shot record was not found.");
  }
  char json[48] = {};
  snprintf(json, sizeof(json), "{\"rated\":true,\"rating\":%u}",
           static_cast<unsigned>(rating));
  return sendJson(request, STATUS_OK, json);
}

esp_err_t ShotStopperNetwork::timeSyncHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before syncing the clock.");
  }
  self.ntpManualSyncPending_ = true;
  return self.sendAccepted(request, self.allocateRequestId());
}

esp_err_t ShotStopperNetwork::configHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Configuration is locked while a cycle is active.");
  }
  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  const bool dateTimePatch =
      jsonFieldPresent(root, "timezoneOffsetMinutes") ||
      jsonFieldPresent(root, "ntpServerPreset") ||
      jsonFieldPresent(root, "ntpServerCustom");
  const bool diagnosticPatch = jsonFieldPresent(root, "serialDebugOutput") ||
                               jsonFieldPresent(root, "ringRetainLogLevel");
  // Patch: seed live effective config; only present keys overwrite.
  RuntimeConfig candidate = {};
  if (self.callbacks_.copyRuntimeConfig != nullptr) {
    self.callbacks_.copyRuntimeConfig(&candidate);
  }
  bool brewByWeightPresent = false;
  bool brewByWeight = !candidate.timerOnly;
  bool baseRevisionPresent = false;
  uint32_t baseRevision = 0;
  bool soundAlertsEnabled = !candidate.soundAlertsMuted;
  BullseyeMelodyConfig candidateBullseye = {};
  if (self.callbacks_.copyBullseyeConfig != nullptr) {
    self.callbacks_.copyBullseyeConfig(&candidateBullseye);
  } else {
    portENTER_CRITICAL(&self.dataMux_);
    candidateBullseye = self.settings_.bullseyeMelody;
    portEXIT_CRITICAL(&self.dataMux_);
  }
  const bool bullseyeEnabledPresent =
      jsonFieldPresent(root, "bullseyeMelodyEnabled");
  const bool bullseyeRtttlPresent = jsonFieldPresent(root, "bullseyeRtttl");
  const bool bullseyeConfigSpecified =
      bullseyeEnabledPresent || bullseyeRtttlPresent;
  bool legacyAvoidBbwShotWithoutScale =
      noScaleBbwEnabled(candidate.noScaleBbwMode);
  bool legacyNoScalePresent = false;
  char customNtp[NTP_SERVER_HOST_CAPACITY] = {};
  memcpy(customNtp, candidate.ntpServerCustom, sizeof(customNtp));
  static const char *const fields[] = {
      "baseRevision", "goalWeightG", "rinseEnabled", "rinseGestureMs", "rinseDurationMs",
      "operationalWallMs", "autoTare", "postTareBaselineGraceMs",
      "brewByWeight", "canTareStartTimer",
      "scaleTimerStopExtraDelayMs",       "dripDelayMs", "soundAlertsEnabled",
      "firstDropBeep", "scaleConnectedLed",
      "paddleReturnReminderBeep",
      "paddleReturnReminderIntervalMs", "paddleReturnReminderMaxDurationMs",
      "paddleMode", "stopPulseMs", "maxSinglePressMs", "momentaryStartEdge",
      "reedConfirmTimeoutMs", "assumeIdleWhenScaleConnects", "shotReactTimeoutS",
      "buzzerScaleLostBeep", "buzzerAutoToManualGuardEndBeep",
      "buzzerManualNoScaleBeep", "buzzerScaleConnectedBeep",
      "buzzerExtendedPulseRate", "buzzerSlowExtendedPulseRate",
      "bullseyeMelodyEnabled", "bullseyeRtttl",
      "alertOutputChannel", "autoRetare", "retareWindowMs", "minimumCupWeightG",
      "cupRemovedWeightG",
      "retareStabilitySamples", "retareStabilityToleranceG",
      "retareStabilityMaxGapMs", "retareStabilityMinDurationMs",
      "bbwProtectionMs", "fastExtractionGuardEnabled", "avoidAccidentalTouchEnabled",
      "maxRecoveryWeightG",
      "minBbwBrewTimeMs", "slowExtractionGuardEnabled", "minRecoveryWeightG",
      "maxBbwBrewTimeMs", "autoToManualGuardEnabled", "autoToManualGuardLimitMode",
      "autoToManualGuardManualLimitMs", "autoToManualGuardBaselineMs",
      "weightOffsetBaselineG", "timezoneOffsetMinutes", "ntpServerPreset",
      "ntpServerCustom", "scaleMacCacheMode", "bookooMuteOnBuzzerOnly",
      "bookooConnectBeepLevel", "noScaleBbwMode", "avoidBbwShotWithoutScale",
      "lastShotCooldownMs",
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
    parseError = jsonParseFailureMessage(
        "Config must be a JSON object with known fields and correct types.");
  } else if (settingFieldCount == 0) {
    parseError = "Config patch must include at least one setting field.";
  } else if (jsonFieldPresent(root, "baseRevision") &&
             !jsonUint32(root, "baseRevision", baseRevision)) {
    parseError = "baseRevision must be an integer.";
  } else if (jsonFieldPresent(root, "goalWeightG") &&
             !jsonUint8(root, "goalWeightG", candidate.goalWeightG)) {
    parseError = "goalWeightG must be an integer from 10 to 200.";
  } else if (jsonFieldPresent(root, "rinseEnabled") &&
             !jsonBoolean(root, "rinseEnabled", candidate.rinseEnabled)) {
    parseError = "rinseEnabled must be a boolean.";
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
  } else if (jsonFieldPresent(root, "postTareBaselineGraceMs") &&
             !jsonUint32(root, "postTareBaselineGraceMs",
                         candidate.postTareBaselineGraceMs)) {
    parseError = "postTareBaselineGraceMs must be an integer (milliseconds).";
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
  } else if (bullseyeEnabledPresent &&
             !jsonBoolean(root, "bullseyeMelodyEnabled",
                          candidateBullseye.enabled)) {
    parseError = "bullseyeMelodyEnabled must be a boolean.";
  } else if (bullseyeRtttlPresent &&
             !jsonString(root, "bullseyeRtttl", candidateBullseye.rtttl,
                         sizeof(candidateBullseye.rtttl), true)) {
    parseError = "bullseyeRtttl must be a string of at most 500 characters.";
  } else if (bullseyeConfigSpecified &&
             !validBullseyeMelodyConfig(candidateBullseye)) {
    parseError = candidateBullseye.enabled && candidateBullseye.rtttl[0] == '\0'
                     ? "Bullseye melody needs an RTTTL tune when enabled."
                     : "bullseyeRtttl must be valid single-line RTTTL (up to 250 notes).";
  } else if (jsonFieldPresent(root, "scaleConnectedLed") &&
             !jsonBoolean(root, "scaleConnectedLed",
                          candidate.scaleConnectedLed)) {
    parseError = "scaleConnectedLed must be a boolean.";
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
  } else if (jsonFieldPresent(root, "stopPulseMs") &&
             !jsonStopPulseMs(root, candidate)) {
    parseError = "stopPulseMs must be an integer from 50 to 1000.";
  } else if (jsonFieldPresent(root, "maxSinglePressMs") &&
             !jsonMaxSinglePressMs(root, candidate)) {
    parseError = "maxSinglePressMs must be an integer from 100 to 5000.";
  } else if (jsonFieldPresent(root, "momentaryStartEdge") &&
             !jsonMomentaryStartEdge(root, candidate)) {
    parseError = "momentaryStartEdge must be press or release.";
  } else if (jsonFieldPresent(root, "reedConfirmTimeoutMs") &&
             !jsonReedConfirmTimeoutMs(root, candidate)) {
    parseError = "reedConfirmTimeoutMs must be an integer from 200 to 5000.";
  } else if (jsonFieldPresent(root, "assumeIdleWhenScaleConnects") &&
             !jsonAssumeIdleWhenScaleConnects(root, candidate)) {
    parseError = "assumeIdleWhenScaleConnects must be a boolean.";
  } else if (jsonFieldPresent(root, "shotReactTimeoutS") &&
             !jsonShotReactTimeoutS(root, candidate)) {
    parseError =
        "shotReactTimeoutS must be 0 (compiled default) or an integer from 3 to 30.";
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
  } else if (jsonFieldPresent(root, "cupRemovedWeightG") &&
             !jsonFloat(root, "cupRemovedWeightG",
                        candidate.cupRemovedWeightG)) {
    parseError = "cupRemovedWeightG must be a number.";
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
  } else if (jsonFieldPresent(root, "avoidAccidentalTouchEnabled") &&
             !jsonBoolean(root, "avoidAccidentalTouchEnabled",
                          candidate.avoidAccidentalTouchEnabled)) {
    parseError = "avoidAccidentalTouchEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "maxRecoveryWeightG") &&
             !jsonFloat(root, "maxRecoveryWeightG",
                        candidate.maxRecoveryWeightG)) {
    parseError = "maxRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "minBbwBrewTimeMs") &&
             !jsonUint32(root, "minBbwBrewTimeMs", candidate.minBbwBrewTimeMs)) {
    parseError = "minBbwBrewTimeMs must be an integer (milliseconds).";
  } else if (jsonFieldPresent(root, "slowExtractionGuardEnabled") &&
             !jsonBoolean(root, "slowExtractionGuardEnabled",
                          candidate.slowExtractionGuardEnabled)) {
    parseError = "slowExtractionGuardEnabled must be a boolean.";
  } else if (jsonFieldPresent(root, "minRecoveryWeightG") &&
             !jsonFloat(root, "minRecoveryWeightG",
                        candidate.minRecoveryWeightG)) {
    parseError = "minRecoveryWeightG must be a number.";
  } else if (jsonFieldPresent(root, "maxBbwBrewTimeMs") &&
             !jsonUint32(root, "maxBbwBrewTimeMs", candidate.maxBbwBrewTimeMs)) {
    parseError = "maxBbwBrewTimeMs must be an integer (milliseconds).";
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
    parseError = "scaleMacCacheMode must be first, prefer, or only.";
  } else if (jsonFieldPresent(root, "bookooMuteOnBuzzerOnly") &&
             !jsonBoolean(root, "bookooMuteOnBuzzerOnly",
                          candidate.bookooMuteOnBuzzerOnly)) {
    parseError = "bookooMuteOnBuzzerOnly must be a boolean.";
  } else if (jsonFieldPresent(root, "bookooConnectBeepLevel") &&
             (!jsonUint8(root, "bookooConnectBeepLevel",
                         candidate.bookooConnectBeepLevel) ||
              candidate.bookooConnectBeepLevel > BOOKOO_BEEP_LEVEL_MAX)) {
    parseError = "bookooConnectBeepLevel must be an integer from 0 to 5.";
  } else if (jsonFieldPresent(root, "noScaleBbwMode") &&
             jsonFieldPresent(root, "avoidBbwShotWithoutScale")) {
    parseError =
        "Send noScaleBbwMode or avoidBbwShotWithoutScale, not both.";
  } else if (jsonFieldPresent(root, "noScaleBbwMode") &&
             !jsonNoScaleBbwMode(root, "noScaleBbwMode",
                                 candidate.noScaleBbwMode)) {
    parseError =
        "noScaleBbwMode must be off, warn_once, or require_scale.";
  } else if (jsonFieldPresent(root, "avoidBbwShotWithoutScale") &&
             !jsonBoolean(root, "avoidBbwShotWithoutScale",
                          legacyAvoidBbwShotWithoutScale)) {
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
    legacyNoScalePresent =
        jsonFieldPresent(root, "avoidBbwShotWithoutScale");
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (parseError != nullptr) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     parseError);
  }
  if (dateTimePatch && !self.requireAdminUnlock(request)) {
    memset(customNtp, 0, sizeof(customNtp));
    return ESP_OK;
  }
  if (diagnosticPatch && !self.requireAdminUnlock(request)) {
    memset(customNtp, 0, sizeof(customNtp));
    return ESP_OK;
  }
  if (baseRevisionPresent && baseRevision != candidate.revision) {
    memset(customNtp, 0, sizeof(customNtp));
    return sendError(request, STATUS_CONFLICT, "CONFIG_REVISION_STALE",
                     "Config changed; refresh and retry.");
  }
  if (brewByWeightPresent) {
    candidate.timerOnly = !brewByWeight;
  }
  if (legacyNoScalePresent) {
    candidate.noScaleBbwMode = static_cast<uint8_t>(
        legacyAvoidBbwShotWithoutScale ? NoScaleBbwMode::WARN_ONCE
                                       : NoScaleBbwMode::OFF);
  }
  candidate.soundAlertsMuted = !soundAlertsEnabled;
  memcpy(candidate.ntpServerCustom, customNtp, sizeof(candidate.ntpServerCustom));
  memset(customNtp, 0, sizeof(customNtp));
  // ControlGateSnapshot is gate-only (no recipe bank). Validate the patched
  // machine config against the live preset overlay, same as APPLY_CONFIG.
  ShotPresetBank livePresets = {};
  if (self.callbacks_.copyPresetBank != nullptr) {
    self.callbacks_.copyPresetBank(&livePresets);
  } else {
    portENTER_CRITICAL(&self.dataMux_);
    livePresets = self.settings_.presets;
    portEXIT_CRITICAL(&self.dataMux_);
  }
  const RuntimeConfig effective =
      composeEffectiveConfig(candidate, livePresets);
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
  command.bullseyeConfigSpecified = bullseyeConfigSpecified;
  if (bullseyeConfigSpecified) {
    if (self.callbacks_.stageBullseyeConfig == nullptr ||
        !self.callbacks_.stageBullseyeConfig(candidateBullseye,
                                              command.requestId)) {
      return sendError(request, STATUS_UNAVAILABLE, "CONTROL_BUSY",
                       "Control is busy; the Bullseye melody was not staged.");
    }
    command.bullseyeStageRequestId = command.requestId;
  }
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::preferredScaleClearHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
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
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The preferred scale cannot be changed while a cycle "
                     "is active.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  char mac[PREFERRED_SCALE_MAC_CAPACITY] = {};
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  if (root == nullptr) {
    parseError = jsonParseFailureMessage("JSON body is required.");
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
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_BAD_REQUEST, "INVALID_REQUEST", parseError);
  }

  WebCommand command;
  command.type = WebCommandType::SELECT_PREFERRED_SCALE;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  copyCString(command.scaleSelectMac, sizeof(command.scaleSelectMac), mac);
  copyCString(command.scaleSelectName, sizeof(command.scaleSelectName), name);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was selected.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::presetsHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Presets are locked while a cycle is active.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "A bounded JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  char action[24] = {};
  uint8_t presetId = 0;
  char presetName[SHOT_PRESET_NAME_CAPACITY] = {};
  const char *parseError = nullptr;
  PresetAction presetAction = PresetAction::APPLY;
  WebCommand command;
  command.type = WebCommandType::PRESET_OP;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  if (self.callbacks_.copyRuntimeConfig != nullptr) {
    self.callbacks_.copyRuntimeConfig(&command.config);
  }

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
          !jsonUint32(root, "minBbwBrewTimeMs", command.config.minBbwBrewTimeMs) ||
          !jsonBoolean(root, "slowExtractionGuardEnabled",
                       command.config.slowExtractionGuardEnabled) ||
          !jsonFloat(root, "minRecoveryWeightG",
                     command.config.minRecoveryWeightG) ||
          !jsonUint32(root, "maxBbwBrewTimeMs", command.config.maxBbwBrewTimeMs) ||
          !jsonBoolean(root, "autoToManualGuardEnabled",
                       command.config.autoToManualGuardEnabled) ||
          !jsonAutoToManualGuardLimitMode(
              root, "autoToManualGuardLimitMode",
              command.config.autoToManualGuardLimitMode) ||
          !jsonUint32(root, "autoToManualGuardManualLimitMs",
                      command.config.autoToManualGuardManualLimitMs) ||
          !jsonUint32(root, "autoToManualGuardBaselineMs",
                      command.config.autoToManualGuardBaselineMs) ||
          !jsonBoolean(root, "cupProtectionEnabled",
                       command.config.cupProtectionEnabled) ||
          !jsonBoolean(root, "stopIfCupRemoved",
                       command.config.stopIfCupRemoved) ||
          !jsonBoolean(root, "requireCupToStart",
                       command.config.requireCupToStart) ||
          !jsonBoolean(root, "avoidAccidentalTouchEnabled",
                       command.config.avoidAccidentalTouchEnabled)) {
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
  self.unlockJsonBody();
  if (parseError != nullptr) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD", parseError);
  }

  command.presetAction = static_cast<uint8_t>(presetAction);
  command.presetId = presetId;
  copyCString(command.presetName, sizeof(command.presetName), presetName);
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control is busy; nothing was saved.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::resetCalibrationHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Calibration is locked while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "An empty JSON object is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const noFields[] = {nullptr};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, noFields, 0);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Guard samples are locked while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "An empty JSON object is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const noFields[] = {nullptr};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, noFields, 0);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON body is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  bool on = false;
  static const char *const fields[] = {"on"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonBoolean(root, "on", on);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "The on field must be boolean.");
  }
  ControlGateSnapshot status = self.controlGate();
  if (on && !REMOTE_MACHINE_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote machine control actuation is disabled in this firmware build.");
  }
  const bool allowed = on ? self.webUiConfigurationAllowed(request, status)
                          : (status.activeCycle &&
                             status.source == ControlSource::WEB);
  if (!allowed) {
    return sendError(request, STATUS_CONFLICT, "CONTROL_STATE_CONFLICT",
                     "The current state does not allow that action.");
  }
  WebCommand command;
  command.type = on ? WebCommandType::REMOTE_ON : WebCommandType::REMOTE_OFF;
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!REMOTE_MACHINE_CONTROL_ENABLED) {
    return sendError(request, "403 Forbidden", "REMOTE_CONTROL_DISABLED",
                     "Remote machine control actuation is disabled in this firmware build.");
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!status.activeCycle || !status.machineRunning) {
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

esp_err_t ShotStopperNetwork::stateOverrideHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (SHOT_STOPPER_MACHINE_TYPE != 1) {
    return sendError(request, STATUS_CONFLICT, "MACHINE_TYPE_UNSUPPORTED",
                     "Inferred-state override is only available on switch-only builds.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON body is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  char state[8] = {};
  static const char *const fields[] = {"state"};
  const bool parsed = root != nullptr &&
                      jsonHasOnlyUniqueFields(root, fields, 1) &&
                      jsonString(root, "state", state, sizeof(state), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed || (strcmp(state, "off") != 0 && strcmp(state, "on") != 0)) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_FIELD",
                     "The state field must be \"off\" or \"on\".");
  }
  WebCommand command;
  command.type = strcmp(state, "off") == 0 ? WebCommandType::STATE_OVERRIDE_OFF
                                           : WebCommandType::STATE_OVERRIDE_ON;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Control queue is full.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::restartHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  // Shot always wins. Queue the restart even mid-cycle; the control task
  // holds it until idle instead of opening K1 to make way for the reset.
  // Override must not cut a pour.
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle, switch the physical activator OFF, and wait for Ready before restoring factory settings.");
  }

  const esp_err_t bodyStatus = self.lockJsonBody(
      request, "An explicit factory-reset confirmation is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  char confirmation[32] = {};
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"confirm"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "confirm", confirmation, sizeof(confirmation), false) &&
      strcmp(confirmation, "ERASE_ALL_SETTINGS") == 0;
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
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
  ControlGateSnapshot status = self.controlGate();
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  char action[16] = {};
  WebCommand command;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  static const char *const forgetFields[] = {"action"};
  static const char *const confirmFields[] = {"action"};
  static const char *const saveFields[] = {
      "action", "ssid",     "password", "open",  "wifiSleep", "ipMode",
      "ip",     "netmask",  "gateway",  "dns1",  "dns2"};
  const char *networkError = nullptr;
  bool parsed = root != nullptr &&
                jsonString(root, "action", action, sizeof(action), false);
  if (parsed && strcmp(action, "confirm") != 0) {
    if (!self.requireAdminUnlock(request)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      self.unlockJsonBody();
      memset(command.password, 0, sizeof(command.password));
      return ESP_OK;
    }
  }
  if (parsed && strcmp(action, "confirm") == 0) {
    parsed = jsonHasOnlyUniqueFields(root, confirmFields, 1);
    if (!parsed) {
      networkError = "Confirm request must include only action=\"confirm\".";
    } else {
      const StaJoinHints sta = self.staJoinHints();
      const NetworkStatusSnapshot network = self.snapshot();
      if (sta.staConfigState !=
              static_cast<uint8_t>(StaConfigState::PENDING) ||
          !sta.staConfigured || network.apActive ||
          network.staState != StaState::CONNECTED) {
        parsed = false;
        networkError = "No pending network configuration to confirm.";
      } else {
        self.requestPendingNetworkConfirm();
        if (root != nullptr) {
          cJSON_Delete(root);
        }
        self.unlockJsonBody();
        return self.sendAccepted(request, command.requestId,
                                 "\"state\":\"QUEUED\"");
      }
    }
  } else if (parsed && strcmp(action, "forget") == 0) {
    if (!self.webUiConfigurationAllowed(request, status)) {
      if (root != nullptr) {
        cJSON_Delete(root);
      }
      self.unlockJsonBody();
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
      self.unlockJsonBody();
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
    if (!jsonHasOnlyUniqueFields(root, saveFields, 11) ||
        !jsonString(root, "ssid", command.ssid, sizeof(command.ssid), false) ||
        !jsonString(root, "password", command.password,
                    sizeof(command.password), true) ||
        !jsonBoolean(root, "open", command.openNetwork) ||
        !jsonBoolean(root, "wifiSleep", command.wifiSleep) ||
        !jsonString(root, "ipMode", ipMode, sizeof(ipMode), false)) {
      parsed = false;
      networkError =
          "Save request requires action, ssid, password, open, wifiSleep, and ipMode.";
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
    if (parsed) {
      command.wifiSleepSpecified = true;
    }
    if (parsed && !validWifiSsid(command.ssid)) {
      parsed = false;
      networkError = "SSID must be 1–32 characters.";
    } else if (parsed) {
      const StaJoinHints sta = self.staJoinHints();
      const bool reusePassword = shouldReuseSavedWifiCredentials(
          command.ssid, command.password, command.openNetwork,
          sta.staConfigured, sta.staSsid, sta.staOpen);
      if (!reusePassword &&
          !validWifiPassword(command.password, command.openNetwork)) {
        parsed = false;
        networkError = command.openNetwork
                           ? "Open network password must be empty."
                           : (sta.staConfigured
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
  self.unlockJsonBody();
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
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
  g_wifiScan.state = WifiScanState::QUEUED;
  portEXIT_CRITICAL(&self.dataMux_);
  
  return self.sendAccepted(request, command.requestId,
                           "\"state\":\"QUEUED\"");
}

esp_err_t ShotStopperNetwork::wifiScanStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  if (!self.lockWorkBuf()) {
    return self.workBufBusy(request);
  }
  WifiScanSnapshot &scan = self.workBuf_->wifiScan;
  portENTER_CRITICAL(&self.dataMux_);
  scan = g_wifiScan;
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
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  for (size_t index = 0; index < scan.count; ++index) {
    if (httpd_resp_send_chunk(request, index == 0 ? "{\"ssid\":" :
                                                  ",{\"ssid\":",
                              HTTPD_RESP_USE_STRLEN) != ESP_OK ||
        sendJsonStringChunk(request, scan.networks[index].ssid) != ESP_OK) {
      self.unlockWorkBuf();
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
      self.unlockWorkBuf();
      return ESP_FAIL;
    }
  }
  if (httpd_resp_send_chunk(request, "]}", 2) != ESP_OK) {
    self.unlockWorkBuf();
    return ESP_FAIL;
  }
  const esp_err_t sent = httpd_resp_send_chunk(request, nullptr, 0);
  self.unlockWorkBuf();
  return sent;
}

esp_err_t ShotStopperNetwork::devicePasswordHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  ControlGateSnapshot status = self.controlGate();
  if (!self.webUiConfigurationAllowed(request, status)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "The password cannot be changed while a cycle is active.");
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  WebCommand command;
  command.type = WebCommandType::CHANGE_DEVICE_PASSWORD;
  command.requestId = self.allocateRequestId();
  command.unsafeWebUiOverride = self.webUiOverrideAllowed(request);
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"newPassword"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonString(root, "newPassword", command.password,
                 sizeof(command.password), false);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password is required.");
  }
  if (!validDevicePassword(command.password)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password must be 8–63 characters.");
  }
  if (isFactoryDefaultPassword(command.password)) {
    memset(command.password, 0, sizeof(command.password));
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_DEVICE_PASSWORD",
                     "New device password cannot be the factory default.");
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
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  static const char *const fields[] = {"enabled", "scanIntensity"};
  bool enabled = false;
  char intensityText[12] = {};
  BleScanIntensity intensity = BleScanIntensity::NORMAL;
  const bool hasEnabled = root != nullptr && jsonFieldPresent(root, "enabled");
  const bool hasScan =
      root != nullptr && jsonFieldPresent(root, "scanIntensity");
  bool parsed = root != nullptr && jsonHasOnlyUniqueFields(root, fields, 2) &&
                (hasEnabled || hasScan);
  if (parsed && hasEnabled) {
    parsed = jsonBoolean(root, "enabled", enabled);
  }
  if (parsed && hasScan) {
    parsed = jsonString(root, "scanIntensity", intensityText,
                        sizeof(intensityText), false) &&
             parseBleScanIntensityId(intensityText, intensity);
  }
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_BLE_CONFIG",
                     "enabled must be a boolean and/or scanIntensity must be "
                     "normal, aggressive, or light.");
  }
  WebCommand command;
  if (hasEnabled) {
    command.type = enabled ? WebCommandType::BLE_COMPAT_ENABLE
                           : WebCommandType::BLE_COMPAT_DISABLE;
  } else {
    command.type = WebCommandType::BLE_SCAN_INTENSITY;
  }
  if (hasScan) {
    command.bleScanIntensitySpecified = true;
    command.bleScanIntensity = static_cast<uint8_t>(intensity);
  }
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "BLE Companion state was not changed.");
  }
  return self.sendAccepted(request, command.requestId);
}

esp_err_t ShotStopperNetwork::taskProfilerHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.requireAdminUnlock(request)) {
    return ESP_OK;
  }
  const esp_err_t bodyStatus =
      self.lockJsonBody(request, "A JSON request is required.");
  if (bodyStatus != ESP_OK) {
    return bodyStatus;
  }
  cJSON *root = parseJsonInArena(self.workBuf_->requestBody);
  bool enabled = false;
  static const char *const fields[] = {"enabled"};
  const bool parsed =
      root != nullptr && jsonHasOnlyUniqueFields(root, fields, 1) &&
      jsonBoolean(root, "enabled", enabled);
  if (root != nullptr) {
    cJSON_Delete(root);
  }
  self.unlockJsonBody();
  if (!parsed) {
    return sendError(request, STATUS_UNPROCESSABLE, "INVALID_PROFILER_CONFIG",
                     "enabled must be a boolean.");
  }
  WebCommand command;
  command.type = enabled ? WebCommandType::TASK_PROFILER_START
                         : WebCommandType::TASK_PROFILER_STOP;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "Task profiler state was not changed.");
  }
  return self.sendAccepted(request, command.requestId);
}

namespace {

struct OtaTransfer {
  ShotStopperNetwork *network = nullptr;
  httpd_req_t *request = nullptr;
};

bool devicePasswordsMatch(const char *candidate, const char *expected) {
  return secretsMatch(candidate, expected);
}

const char *otaResultHttpStatus(OtaResult result) {
  switch (result) {
    case OtaResult::OK: return STATUS_OK;
    case OtaResult::UNAVAILABLE:
    case OtaResult::BUSY:
    case OtaResult::PENDING_VERIFY:
    case OtaResult::SAFETY_LOST:
    case OtaResult::NO_IDENTITY:
    case OtaResult::NOTHING_STAGED: return STATUS_CONFLICT;
    case OtaResult::TOO_LARGE: return STATUS_TOO_LARGE;
    case OtaResult::BAD_LENGTH:
    case OtaResult::BAD_IMAGE:
    case OtaResult::NO_TAG:
    case OtaResult::ARCH_MISMATCH:
    case OtaResult::DOWNGRADE: return STATUS_UNPROCESSABLE;
    case OtaResult::RECEIVE_FAILED: return STATUS_BAD_REQUEST;
    case OtaResult::WRITE_FAILED:
    case OtaResult::VERIFY_FAILED:
    case OtaResult::COMMIT_FAILED:
    case OtaResult::NO_MEMORY:
    case OtaResult::INTERNAL: return STATUS_SERVER_ERROR;
  }
  return STATUS_SERVER_ERROR;
}

const char *otaResultMessage(OtaResult result) {
  switch (result) {
    case OtaResult::OK: return "The firmware image was verified.";
    case OtaResult::UNAVAILABLE:
      return "This controller has no second firmware slot, so it cannot be "
             "updated over Wi-Fi.";
    case OtaResult::BUSY: return "Another firmware update is already running.";
    case OtaResult::PENDING_VERIFY:
      return "The running firmware has not been confirmed yet. Wait a moment "
             "and try again.";
    case OtaResult::BAD_LENGTH:
      return "The upload is too small to be a firmware image.";
    case OtaResult::TOO_LARGE:
      return "The firmware image does not fit in the update slot.";
    case OtaResult::RECEIVE_FAILED:
      return "The upload was interrupted. The running firmware was not "
             "touched.";
    case OtaResult::BAD_IMAGE:
      return "This file is not an ESP32-S3 application image.";
    case OtaResult::NO_TAG:
      return "This image is not a Shot Stopper build.";
    case OtaResult::NO_IDENTITY:
      return "The running firmware does not say which board it was built for, "
             "so no image can be checked against it. Reflash over USB with "
             "./scripts/build --arch <board> to update over Wi-Fi again.";
    case OtaResult::ARCH_MISMATCH:
      return "This image was built for a different controller board.";
    case OtaResult::DOWNGRADE:
      return "This image is older than the running firmware.";
    case OtaResult::WRITE_FAILED:
      return "The update slot could not be written. The running firmware was "
             "not touched.";
    case OtaResult::VERIFY_FAILED:
      return "The uploaded image failed its checksum. Nothing was flashed.";
    case OtaResult::SAFETY_LOST:
      return "The paddle moved or a shot started during the upload, so the "
             "update was cancelled. The verified image was not kept.";
    case OtaResult::NOTHING_STAGED:
      return "Upload and verify a firmware image first.";
    case OtaResult::COMMIT_FAILED:
      return "The bootloader refused the staged image. Nothing was changed.";
    case OtaResult::NO_MEMORY:
      return "Not enough memory to receive a firmware image right now.";
    case OtaResult::INTERNAL:
      return "This firmware cannot verify updates. Reflash over USB.";
  }
  return "The firmware update failed.";
}

void appendOtaTag(char *buffer, size_t capacity, size_t &used,
                  const char *name, const OtaImageTag &tag, bool valid) {
  if (used >= capacity) {
    return;
  }
  int written = 0;
  if (valid && tag.valid) {
    written = snprintf(buffer + used, capacity - used,
                       ",\"%s\":{\"arch\":\"%s\",\"version\":\"%s\","
                       "\"packed\":%lu}",
                       name, tag.arch, tag.version,
                       static_cast<unsigned long>(tag.packed));
  } else {
    written = snprintf(buffer + used, capacity - used, ",\"%s\":null", name);
  }
  if (written > 0 && static_cast<size_t>(written) < capacity - used) {
    used += static_cast<size_t>(written);
  }
}

}  // namespace

bool ShotStopperNetwork::authorizeOtaRequest(httpd_req_t *request) {
  char password[WIFI_PASSWORD_CAPACITY] = {};
  char expected[WIFI_PASSWORD_CAPACITY] = {};
  const char *header = DEVICE_PASSWORD_HEADER;
  size_t length = httpd_req_get_hdr_value_len(request, header);
  if (length == 0) {
    header = DEVICE_PASSWORD_HEADER_LEGACY;
    length = httpd_req_get_hdr_value_len(request, header);
  }
  bool passwordAuthorized = false;
  if (length > 0 && length + 1 <= sizeof(password) &&
      httpd_req_get_hdr_value_str(request, header, password,
                                  sizeof(password)) == ESP_OK) {
    portENTER_CRITICAL(&dataMux_);
    memcpy(expected, settings_.devicePassword, sizeof(expected));
    portEXIT_CRITICAL(&dataMux_);
    passwordAuthorized = devicePasswordsMatch(password, expected);
  }
  memset(password, 0, sizeof(password));
  memset(expected, 0, sizeof(expected));
  if (passwordAuthorized) {
    return true;
  }
  if (adminUnlockAllowed(request)) {
    touchAdminUnlock();
    return true;
  }
  return false;
}

void ShotStopperNetwork::buildOtaJson(char *buffer, size_t capacity,
                                      const ControlGateSnapshot &control) {
  if (buffer == nullptr || capacity == 0) {
    return;
  }
  buffer[0] = '\0';
  ShotStopperOta &ota = ShotStopperOta::instance();
  const OtaStatusSnapshot ota_ = ota.snapshot();
  const bool safe = controlAllowsConfiguration(control);
  int written = snprintf(
      buffer, capacity,
      "{\"available\":%s,\"state\":\"%s\",\"slotBytes\":%lu,"
      "\"receivedBytes\":%lu,\"expectedBytes\":%lu,\"pendingVerify\":%s,"
      "\"confirmed\":%s,\"safe\":%s,\"lockReason\":\"%s\","
      "\"passwordRequired\":true,\"passwordAvailable\":true,\"restartPending\":%s",
      ota.available() ? "true" : "false",
      ShotStopperOta::stateName(ota_.state),
      static_cast<unsigned long>(ota_.slotBytes),
      static_cast<unsigned long>(ota_.receivedBytes),
      static_cast<unsigned long>(ota_.expectedBytes),
      ota_.pendingVerify ? "true" : "false",
      ota_.confirmed ? "true" : "false", safe ? "true" : "false",
      configLockReason(control),
      otaRestartPending_ ? "true" : "false");
  if (written <= 0 || static_cast<size_t>(written) >= capacity) {
    snprintf(buffer, capacity, "{\"available\":false}");
    return;
  }
  // One byte is held back so the closing brace always has somewhere to go: an
  // object left unclosed here is spliced into the admin status and would make
  // the whole Admin page unparseable, taking unrelated settings down with it.
  size_t used = static_cast<size_t>(written);
  const size_t tagCapacity = capacity - 1;
  appendOtaTag(buffer, tagCapacity, used, "running", ota.runningTag(),
               ota.runningTag().valid);
  appendOtaTag(buffer, tagCapacity, used, "staged", ota_.staged,
               ota_.stagedValid);
  if (used + 2 > capacity) {
    snprintf(buffer, capacity, "{\"available\":false}");
    return;
  }
  buffer[used++] = '}';
  buffer[used] = '\0';
}

esp_err_t ShotStopperNetwork::sendOtaSnapshot(httpd_req_t *request,
                                              const char *httpStatus) {
  const ControlGateSnapshot control = controlGate();
  if (!lockWorkBuf()) {
    return workBufBusy(request);
  }
  buildOtaJson(workBuf_->otaJson, sizeof(workBuf_->otaJson), control);
  const esp_err_t sent = sendJson(request, httpStatus, workBuf_->otaJson);
  unlockWorkBuf();
  return sent;
}

int ShotStopperNetwork::otaReadChunk(void *context, uint8_t *buffer,
                                     size_t capacity) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->request == nullptr) {
    return -1;
  }
  // httpd's socket timeout is deliberately short so stuck clients release
  // sockets; a few retries absorb the stalls that Wi-Fi/BLE coexistence adds
  // without letting a genuinely dead peer hold the transfer open.
  for (uint8_t attempt = 0; attempt < OTA_RECEIVE_ATTEMPTS; ++attempt) {
    const int received = httpd_req_recv(
        transfer->request, reinterpret_cast<char *>(buffer), capacity);
    if (received > 0) {
      return received;
    }
    if (received != HTTPD_SOCK_ERR_TIMEOUT) {
      return -1;
    }
  }
  return -1;
}

bool ShotStopperNetwork::otaTransferStillSafe(void *context) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->network == nullptr) {
    return false;
  }
  return controlAllowsConfiguration(transfer->network->controlGate());
}

void ShotStopperNetwork::otaTransferProgress(void *context, uint32_t received,
                                             uint32_t expected) {
  OtaTransfer *transfer = static_cast<OtaTransfer *>(context);
  if (transfer == nullptr || transfer->network == nullptr) {
    return;
  }
  transfer->network->actionLogf("ota: received %lu/%lu KiB",
                                static_cast<unsigned long>(received / 1024U),
                                static_cast<unsigned long>(expected / 1024U));
  transfer->network->touchAdminUnlock();
}

esp_err_t ShotStopperNetwork::otaStatusHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  return self.sendOtaSnapshot(request, STATUS_OK);
}

esp_err_t ShotStopperNetwork::otaUploadHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  auto rejectUpload = [request](const char *status, const char *error,
                                const char *message) {
    const esp_err_t sent = sendError(request, status, error, message);
    // ESP-IDF otherwise purges the unread body 32 bytes at a time on the
    // only httpd task, so a 2 MB POST with a bad token would freeze the
    // Web UI for tens of seconds. Closing the socket drops the rest.
    if (request->content_len > 0) {
      httpd_sess_trigger_close(request->handle,
                               httpd_req_to_sockfd(request));
    }
    return sent;
  };
  if (!self.authorizeOtaRequest(request)) {
    return rejectUpload(STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                        "Send the device password, or unlock administration first.");
  }
  ShotStopperOta &ota = ShotStopperOta::instance();

  // A firmware update never honours the unsafe WebUI override: unlike a
  // setting, it cannot be undone from the Web UI if the machine is mid-shot.
  const ControlGateSnapshot control = self.controlGate();
  if (!controlAllowsConfiguration(control)) {
    return rejectUpload(STATUS_CONFLICT, "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                        "Stop the cycle and wait for Ready before updating "
                        "firmware.");
  }
  if (self.otaRestartPending_) {
    return rejectUpload(STATUS_CONFLICT, "OTA_RESTART_PENDING",
                        "A firmware image is already flashed and waiting for "
                        "the restart.");
  }
  if (request->content_len == 0) {
    return sendError(request, STATUS_BAD_REQUEST, "OTA_LENGTH_REQUIRED",
                     "A Content-Length is required.");
  }

  bool allowDowngrade = false;
  char downgrade[8] = {};
  const size_t downgradeLength =
      httpd_req_get_hdr_value_len(request, OTA_ALLOW_DOWNGRADE_HEADER);
  if (downgradeLength > 0 && downgradeLength + 1 <= sizeof(downgrade) &&
      httpd_req_get_hdr_value_str(request, OTA_ALLOW_DOWNGRADE_HEADER,
                                  downgrade, sizeof(downgrade)) == ESP_OK) {
    allowDowngrade = strcmp(downgrade, "yes") == 0;
  }

  self.log(DebugCategory::NETWORK, DebugCode::OTA_UPLOAD_STARTED,
           static_cast<int32_t>(request->content_len / 1024U));

  OtaTransfer transfer;
  transfer.network = &self;
  transfer.request = request;
  OtaStreamIo io;
  io.read = otaReadChunk;
  io.stillSafe = otaTransferStillSafe;
  io.progress = otaTransferProgress;
  io.context = &transfer;

  const OtaResult result =
      ota.stage(static_cast<uint32_t>(request->content_len), allowDowngrade,
                io);
  if (result != OtaResult::OK) {
    const OtaStatusSnapshot failed = ota.snapshot();
    self.log(DebugCategory::NETWORK, DebugCode::OTA_UPLOAD_REJECTED,
             static_cast<int32_t>(result),
             static_cast<int32_t>(failed.receivedBytes / 1024U));
    return sendError(request, otaResultHttpStatus(result),
                     ShotStopperOta::resultName(result),
                     otaResultMessage(result));
  }
  const OtaStatusSnapshot staged = ota.snapshot();
  self.log(DebugCategory::NETWORK, DebugCode::OTA_IMAGE_STAGED,
           static_cast<int32_t>(staged.receivedBytes / 1024U),
           static_cast<int32_t>(staged.staged.packed));
  return self.sendOtaSnapshot(request, STATUS_OK);
}

esp_err_t ShotStopperNetwork::otaFlashHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  ShotStopperOta &ota = ShotStopperOta::instance();
  const ControlGateSnapshot control = self.controlGate();
  if (!controlAllowsConfiguration(control)) {
    return sendError(request, STATUS_CONFLICT,
                     "CONFIG_LOCKED_DURING_ACTIVE_CYCLE",
                     "Stop the cycle and wait for Ready before flashing.");
  }
  if (self.otaRestartPending_) {
    return sendError(request, STATUS_CONFLICT, "OTA_RESTART_PENDING",
                     "The flashed image is already waiting for the restart.");
  }
  const OtaImageTag staged = ota.snapshot().staged;
  const OtaResult result = ota.commit();
  if (result != OtaResult::OK) {
    return sendError(request, otaResultHttpStatus(result),
                     ShotStopperOta::resultName(result),
                     otaResultMessage(result));
  }
  self.log(DebugCategory::NETWORK, DebugCode::OTA_FLASH_COMMITTED,
           static_cast<int32_t>(staged.packed));

  // The restart runs through the ordinary maintenance lease after the shot
  // (if any) ends. The control task will not open K1 to make way for it.
  WebCommand command;
  command.type = WebCommandType::RESTART;
  command.requestId = self.allocateRequestId();
  if (!self.callbacks_.enqueueWebCommand(command)) {
    // The boot slot is already switched, so the new firmware comes up on the
    // next restart however it happens.
    return sendError(request, STATUS_UNAVAILABLE, "CONTROL_QUEUE_FULL",
                     "The image is flashed. Restart the controller to use it.");
  }
  self.otaRestartRequestedAtMs_ = millis();
  self.otaRestartPending_ = true;
  return self.sendOtaSnapshot(request, STATUS_ACCEPTED);
}

esp_err_t ShotStopperNetwork::otaAbortHandler(httpd_req_t *request) {
  ShotStopperNetwork &self = *instance_;
  if (!self.authorizeOtaRequest(request)) {
    return sendError(request, STATUS_UNAUTHORIZED, "DEVICE_PASSWORD_INVALID",
                     "Send the device password, or unlock administration first.");
  }
  ShotStopperOta::instance().discard();
  return self.sendOtaSnapshot(request, STATUS_OK);
}

void ShotStopperNetwork::serviceOtaRollback(uint32_t now) {
  // A committed image boots on the next restart however it happens, so a
  // restart the control task never granted must not leave the panel stuck
  // claiming one is on the way.
  if (otaRestartPending_ &&
      static_cast<uint32_t>(now - otaRestartRequestedAtMs_) >=
          OTA_RESTART_GIVE_UP_MS) {
    otaRestartPending_ = false;
    actionLog("ota: the restart never happened; restart manually to use the "
              "flashed image");
  }

  ShotStopperOta &ota = ShotStopperOta::instance();
  const bool alreadySettled =
      ota.runningImageRejected() || ota.runningImageConfirmed() || ota.busy();
  // Assume a previous slot exists until the deadline forces a real check.
  // Passing false here would KEEP_RUNNING at 180 s without asking IDF.
  // Otadata writes disable flash cache and drop BLE. Wait while GATT is up
  // or a shot is pouring — the scale stream must not die mid-shot.
  const ControlGateSnapshot control = controlGate();
  const bool flashWriteSafe =
      !scaleConnectingOrUp_.load(std::memory_order_relaxed) &&
      !control.activeCycle && !control.relayClosed;
  const OtaPendingVerifyAction action = decideOtaPendingVerify(
      ota.bootPendingVerify(), alreadySettled,
      startupComplete_ && server_ != nullptr, now, OTA_CONFIRM_MIN_UPTIME_MS,
      OTA_CONFIRM_DEADLINE_MS, true, flashWriteSafe);
  if (action == OtaPendingVerifyAction::NONE ||
      action == OtaPendingVerifyAction::WAIT) {
    return;
  }
  // Deadline still asks to settle, but a live shot always defers the write.
  if (control.activeCycle || control.relayClosed) {
    return;
  }
  if (action == OtaPendingVerifyAction::CONFIRM) {
    if (ota.confirmRunningImage()) {
      log(DebugCategory::NETWORK, DebugCode::OTA_IMAGE_CONFIRMED);
      actionLog("ota: running image confirmed");
    }
    return;
  }
  if (!ota.rejectRunningImage()) {
    // No other slot holds a bootable application. Restarting would leave the
    // machine with nothing to run, so keep this image and make it permanent.
    ota.confirmRunningImage();
    log(DebugCategory::NETWORK, DebugCode::OTA_ROLLBACK_FAILED);
    actionLog("ota: rollback impossible; keeping the running image");
    return;
  }
  log(DebugCategory::NETWORK, DebugCode::OTA_ROLLBACK_ARMED,
      static_cast<int32_t>(now / 1000U));
  actionLog("ota: no Web UI after the update; rolling back on restart");
  otaRollbackRestartPending_ = true;
  restartPending_ = true;
  restartRequestedAtMs_ = now;
}

}  // namespace shotstopper
