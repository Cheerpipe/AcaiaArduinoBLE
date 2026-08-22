#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperBleCompanion.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperShotCurveTypes.h"
#include "ShotStopperShotLogTypes.h"
#include "ShotStopperTime.h"

#include <WiFi.h>
#include <esp_http_server.h>

struct timeval;

namespace shotstopper {

enum class StaState : uint8_t {
  NOT_CONFIGURED,
  CONNECTING,
  CONNECTED,
  FAILED,
  DISCONNECTED
};

enum class WifiScanState : uint8_t {
  IDLE,
  QUEUED,
  RUNNING,
  READY,
  FAILED,
  CANCELED
};

constexpr size_t MAX_WIFI_SCAN_RESULTS = 12;
constexpr size_t kNetworkLogBatchSize = 32;

struct WifiScanNetwork {
  char ssid[WIFI_SSID_CAPACITY] = {};
  int32_t rssi = 0;
  uint8_t channel = 0;
  bool open = false;
};

struct WifiScanSnapshot {
  WifiScanState state = WifiScanState::IDLE;
  uint32_t updatedAtMs = 0;
  uint8_t count = 0;
  WifiScanNetwork networks[MAX_WIFI_SCAN_RESULTS] = {};
};

struct NetworkStatusSnapshot {
  bool networkActive = false;
  bool apActive = false;
  bool wifiConfigured = false;
  bool staOpen = false;
  bool staLinkMetricsValid = false;
  uint8_t apClients = 0;
  StaState staState = StaState::NOT_CONFIGURED;
  uint8_t staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  int8_t staRssi = 0;
  uint8_t staSignalQualityPct = 0;
  uint32_t windowRemainingMs = 0;
  uint32_t confirmRemainingMs = 0;
  uint32_t taskAgeMs = 0;
  uint32_t taskStackMinWords = 0;
  uint32_t startupFailures = 0;
  uint32_t lastCommandRequestId = 0;
  CommandResultState lastCommandState = CommandResultState::NONE;
  bool httpActive = false;
  bool staReconnectHeld = false;
  bool apStartHeld = false;
  bool httpStartHeld = false;
  bool devicePasswordFactory = false;
  bool ntpMayArm = false;
  uint8_t wifiMode = 0;
  uint8_t channel = 0;
  uint8_t scanState = 0;
  uint8_t ntpState = 0;
  int32_t wifiStatus = 6;
  uint32_t staConnectAgeMs = 0;
  uint32_t staReconnectAgeMs = 0;
  char apIp[16] = "192.168.4.1";
  char staIp[16] = {};
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char configuredIp[16] = {};
  char configuredNetmask[16] = {};
  char configuredGateway[16] = {};
  char configuredDns1[16] = {};
  char configuredDns2[16] = {};
  char staMac[18] = {};
  char staBssid[18] = {};
  char apMac[18] = {};
  char ntpActiveServer[NTP_SERVER_HOST_CAPACITY] = {};
};

inline uint8_t wifiRssiToSignalQualityPct(int32_t rssi) {
  if (rssi <= -100) {
    return 0;
  }
  if (rssi >= -50) {
    return 100;
  }
  return static_cast<uint8_t>(2 * (rssi + 100));
}

inline int8_t clampWifiRssi(int32_t rssi) {
  if (rssi < -128) {
    return -128;
  }
  if (rssi > 127) {
    return 127;
  }
  return static_cast<int8_t>(rssi);
}

struct NetworkWorkBuf;

struct NetworkBridgeCallbacks {
  void (*copyControlStatus)(ControlStatusSnapshot &output) = nullptr;
  void (*copyControlGate)(ControlGateSnapshot &output) = nullptr;
  bool (*enqueueWebCommand)(const WebCommand &command) = nullptr;
  size_t (*copyDebugEvents)(uint32_t afterSequence, DebugEvent *output,
                            size_t capacity) = nullptr;
  void (*addDebugEvent)(DebugCategory category, DebugCode code,
                        int32_t argument1, int32_t argument2) = nullptr;
  void (*reportTaskWatchdogFault)() = nullptr;
  void (*requestSafeRestart)() = nullptr;
  size_t (*copyShotRecords)(ShotLogRecord *output, size_t capacity) = nullptr;
  size_t (*copyShotCurves)(ShotCurveRecord *output, size_t capacity) = nullptr;
  bool (*deleteShotRecord)(uint32_t id) = nullptr;
  bool (*clearShotLog)() = nullptr;
  bool (*clearLastShot)() = nullptr;
  bool (*resetAllDurableStores)(PersistedSettings &settings) = nullptr;
  // Keeps NVS Wi-Fi/runtime saves from overwriting a newer preferred scale MAC.
  void (*copyPreferredScaleMac)(char *out, size_t capacity) = nullptr;
  void (*copyPreferredScaleName)(char *out, size_t capacity) = nullptr;
  void (*copyScaleHistory)(ScaleHistoryEntry *out) = nullptr;
  void (*copyPresetBank)(ShotPresetBank *out) = nullptr;
  void (*copyRuntimeConfig)(RuntimeConfig *out) = nullptr;
};

class ShotStopperNetwork {
 public:
  ShotStopperNetwork() = default;
  ShotStopperNetwork(const ShotStopperNetwork &) = delete;
  ShotStopperNetwork &operator=(const ShotStopperNetwork &) = delete;

  bool begin(const PersistedSettings &settings,
             const NetworkBridgeCallbacks &callbacks);
  bool enqueueAcceptedCommand(const WebCommand &command);
  NetworkStatusSnapshot snapshot();
  void requestNtpSyncIfNeeded();
  void syncPreferredScaleMac(const char *mac);
  void syncPreferredScale(const char *mac, const char *name);
  void syncLiveRuntime(const RuntimeConfig &runtime,
                       const ShotPresetBank *presets);
  void syncDurableStorageRevision(uint32_t storageRevision);
  PersistedSettings settingsCopy();

  private:
  void mergePreferredScaleMac(PersistedSettings &settings);
  void overlayLiveShotSettings(PersistedSettings &settings);
  bool serialDebugEnabled() const;
  static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 25000;
  static constexpr uint32_t STA_RECOVERY_ATTEMPT_MS = 60000;
  static constexpr uint32_t STA_CONFIRM_TIMEOUT_MS = 180000;
  static constexpr uint32_t STA_RECONNECT_INTERVAL_MS = 10000;
  static constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 20000;
  static constexpr uint32_t RESTART_DELAY_MS = 750;
  // A freshly committed OTA image proves itself by serving its Web UI. Confirm
  // no earlier than this so a crash during startup still rolls back...
  static constexpr uint32_t OTA_CONFIRM_MIN_UPTIME_MS = 15000;
  // ...and give up if HTTP never comes up, which would otherwise strand the
  // machine on firmware that can no longer be updated over the air.
  static constexpr uint32_t OTA_CONFIRM_DEADLINE_MS = 180000;
  static constexpr uint8_t OTA_RECEIVE_ATTEMPTS = 3;
  static constexpr uint32_t OTA_RESTART_GIVE_UP_MS = 300000;
  static constexpr uint32_t NETWORK_RETRY_MIN_MS = 1000;
  static constexpr uint32_t NETWORK_RETRY_MAX_MS = 30000;
  static constexpr uint32_t COMMAND_RETRY_MIN_MS = 250;
  static constexpr uint32_t MAINTENANCE_PUBLICATION_TIMEOUT_MS = 2000;
  static constexpr uint32_t HTTP_RETRY_MS = 1000;
  static constexpr uint32_t HEALTH_TELEMETRY_INTERVAL_MS = 5000;
  static constexpr uint8_t COMMAND_MAX_ATTEMPTS = 5;
  // Admin unlock is RAM-only and tied to the exclusive WebUI claim. Idle
  // slides on Admin page polls and privileged APIs, not on Home polls.
  static constexpr uint32_t ADMIN_UNLOCK_IDLE_MS = 15 * 60 * 1000;
  static constexpr uint32_t ADMIN_UNLOCK_COOLDOWN_MS = 30000;
  static constexpr uint8_t ADMIN_UNLOCK_FAILURES_BEFORE_COOLDOWN = 5;
  // Machine config JSON is ~1 KiB and grows with NTP custom / bool false
  // literals; keep headroom above the wire payload.
  static constexpr size_t REQUEST_BODY_CAPACITY = 2048;
  static constexpr size_t LOG_BATCH_SIZE = kNetworkLogBatchSize;
  static constexpr size_t WEB_UI_CLIENT_ID_CAPACITY = 25;

  static ShotStopperNetwork *instance_;

  PersistedSettings settings_ = {};
  NetworkBridgeCallbacks callbacks_ = {};
  QueueHandle_t acceptedCommandQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  SemaphoreHandle_t statusResponseMux_ = nullptr;
  NetworkWorkBuf *workBuf_ = nullptr;
  httpd_handle_t server_ = nullptr;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  char activeWebUiClientId_[WEB_UI_CLIENT_ID_CAPACITY] = {};
  bool webUiOverrideActive_ = false;
  char adminUnlockClientId_[WEB_UI_CLIENT_ID_CAPACITY] = {};
  bool adminUnlocked_ = false;
  uint32_t adminUnlockUntilMs_ = 0;
  uint32_t adminUnlockCooldownUntilMs_ = 0;
  uint8_t adminUnlockFailures_ = 0;
  NetworkStatusSnapshot status_ = {};
  bool startupComplete_ = false;
  // Latched for process lifetime after first STA CONNECTED. SoftAP auto-raise
  // is boot/bootstrap only; never cleared by startStation / pending revert.
  bool staEverConnected_ = false;
  bool scanRequested_ = false;
  bool restartPending_ = false;
  bool apRestartPending_ = false;
  bool acceptedCommandPending_ = false;
  bool completionPending_ = false;
  bool staConfirmArmed_ = false;
  bool pendingConfirmRequest_ = false;
  bool staReconnectHeld_ = false;
  bool apStartHeld_ = false;
  bool apKeepRequested_ = false;
  bool httpStartHeld_ = false;
  bool staWifiCoexPreferred_ = false;
  bool otaRestartPending_ = false;
  bool otaRollbackRestartPending_ = false;
  uint32_t otaRestartRequestedAtMs_ = 0;
  WebCommand acceptedCommand_ = {};
  WebCommand completionCommand_ = {};
  uint8_t acceptedCommandAttempts_ = 0;
  uint8_t startupFailures_ = 0;
  uint32_t acceptedCommandReceivedAtMs_ = 0;
  uint32_t acceptedCommandRetryAtMs_ = 0;
  uint32_t networkRetryAtMs_ = 0;
  uint32_t httpRetryAtMs_ = 0;
  uint32_t lastTaskProgressAtMs_ = 0;
  uint32_t taskStackMinWords_ = 0;
  uint32_t nextRequestId_ = 1;
  uint32_t scanMaintenanceLeaseId_ = 0;
  uint32_t scanRequestId_ = 0;
  uint32_t networkStartedAtMs_ = 0;
  uint32_t staConnectStartedAtMs_ = 0;
  uint32_t staReconnectAttemptAtMs_ = 0;
  uint32_t staConfirmDeadlineMs_ = 0;
  uint32_t restartRequestedAtMs_ = 0;
  bool ntpStarted_ = false;
  bool ntpRearmPending_ = false;
  bool ntpManualSyncPending_ = false;
  bool ntpActivitySyncPending_ = false;
  uint8_t ntpFailoverIndex_ = 0;
  uint32_t ntpSyncStartedAtMs_ = 0;
  uint32_t staNtpEligibleAtMs_ = 0;
  uint32_t ntpConfigRevision_ = 0;
  char ntpServerBuffer_[NTP_SERVER_HOST_CAPACITY] = {};

  static void taskEntry(void *parameter);
  void taskLoop();
  void service();
  void serviceNtp(uint32_t now, bool staConnected);
  bool ntpMayArm(uint32_t now, bool staConnected) const;
  void stopNtp();
  void armNtp(uint32_t now);
  void handleNtpFailure(uint32_t now);
  static void ntpSyncNotificationCallback(struct timeval *tv);
  bool startNetwork();
  void startStation(const PersistedSettings &settings, uint32_t now);
  void applyStationAddressConfig(const PersistedSettings &settings);
  void beginStationConnect(const PersistedSettings &settings, uint32_t now);
  void clearStaLinkMetrics();
  void preferStaWifiCoex(bool enable);
  bool ensureAccessPoint(uint32_t now, bool force = false);
  void stopSoftApKeepStation();
  void stopSoftApLeaveHttp();
  void stopSoftAp(bool stopHttp);
  bool wifiScanInProgress();
  void abortWifiScan(uint32_t now, bool logTimeout);
  void stopNetwork();
  bool startHttpServer();
  void stopHttpServer();
  void serviceStaState(uint32_t now);
  void serviceWifiScan(uint32_t now);
  void finishWifiScan(int16_t resultCount, uint32_t now);
  void processAcceptedCommands();
  void processAcceptedMaintenanceCommand(uint32_t now);
  bool processAcceptedCommand(const WebCommand &command);
  bool processPersistedCommand(const WebCommand &command);
  void publishConfiguredAddressStatus();
  void armPendingConfirmWindow(uint32_t now);
  void clearPendingConfirmWindow();
  void requestPendingNetworkConfirm();
  bool confirmPendingNetwork(const char *reason);
  bool revertPendingNetwork(uint32_t now, const char *reason);
  bool controlAllowsNetworkMutation();
  ControlGateSnapshot controlGate() const;
  void log(DebugCategory category, DebugCode code, int32_t argument1 = 0,
           int32_t argument2 = 0);
  void actionLog(const char *message);
  void actionLogf(const char *fmt, ...);
  void lifecycleLog(const char *message);
  void lifecycleLogf(const char *fmt, ...);
  void refreshExtendedStatus(uint32_t now);
  bool handleCliNetworkAction(const WebCommand &command, uint32_t now);
  bool handleCliWifiAction(const WebCommand &command, uint32_t now);
  bool handleCliApAction(const WebCommand &command, uint32_t now);
  bool handleCliWebUiAction(const WebCommand &command, uint32_t now);
  void printActionSnapshot(const char *command, bool ok);
  void noteCliNetworkProgress();

  bool enqueueMaintenanceCompletion(const WebCommand &command,
                                    bool succeeded,
                                    CommandResultState failureState =
                                        CommandResultState::FAILED);
  void recordCommandResult(uint32_t requestId, CommandResultState state);
  esp_err_t sendAccepted(httpd_req_t *request, uint32_t requestId,
                         const char *extraJson = nullptr);
  uint32_t allocateRequestId();

  static esp_err_t rootHandler(httpd_req_t *request);
  static esp_err_t jsHandler(httpd_req_t *request);
  static esp_err_t cssHandler(httpd_req_t *request);
  static esp_err_t runtimeJsHandler(httpd_req_t *request);
  static esp_err_t secondaryJsHandler(httpd_req_t *request);
  static esp_err_t partialHistoryHandler(httpd_req_t *request);
  static esp_err_t partialDiagnosticHandler(httpd_req_t *request);
  static esp_err_t partialSettingsHandler(httpd_req_t *request);
  static esp_err_t partialAdminHandler(httpd_req_t *request);
  static esp_err_t viewSettingsHandler(httpd_req_t *request);
  static esp_err_t browserIconHandler(httpd_req_t *request);
  static esp_err_t notFoundHandler(httpd_req_t *request, httpd_err_code_t error);
  static esp_err_t claimHandler(httpd_req_t *request);
  static esp_err_t unlockHandler(httpd_req_t *request);
  static esp_err_t adminUnlockHandler(httpd_req_t *request);
  static esp_err_t adminLockHandler(httpd_req_t *request);
  static esp_err_t ownedApiHandler(httpd_req_t *request);
  static esp_err_t statusHandler(httpd_req_t *request);
  static esp_err_t logHandler(httpd_req_t *request);
  static esp_err_t shotsHandler(httpd_req_t *request);
  static esp_err_t shotsClearHandler(httpd_req_t *request);
  static esp_err_t shotsDeleteHandler(httpd_req_t *request);
  static esp_err_t lastShotClearHandler(httpd_req_t *request);
  static esp_err_t timeSyncHandler(httpd_req_t *request);
  static esp_err_t configHandler(httpd_req_t *request);
  static esp_err_t preferredScaleClearHandler(httpd_req_t *request);
  static esp_err_t preferredScaleSelectHandler(httpd_req_t *request);
  static esp_err_t presetsHandler(httpd_req_t *request);
  static esp_err_t resetCalibrationHandler(httpd_req_t *request);
  static esp_err_t resetGuardSamplesHandler(httpd_req_t *request);
  static esp_err_t paddleHandler(httpd_req_t *request);
  static esp_err_t rinseHandler(httpd_req_t *request);
  static esp_err_t stopHandler(httpd_req_t *request);
  static esp_err_t restartHandler(httpd_req_t *request);
  static esp_err_t factoryResetHandler(httpd_req_t *request);
  static esp_err_t networkHandler(httpd_req_t *request);
  static esp_err_t wifiScanStartHandler(httpd_req_t *request);
  static esp_err_t wifiScanStatusHandler(httpd_req_t *request);
  static esp_err_t devicePasswordHandler(httpd_req_t *request);
  static esp_err_t bleCompatHandler(httpd_req_t *request);
  // OTA routes authenticate with the device password instead of the
  // exclusive WebUI claim, so the command line client works without stealing
  // control from an open browser window.
  static esp_err_t otaStatusHandler(httpd_req_t *request);
  static esp_err_t otaUploadHandler(httpd_req_t *request);
  static esp_err_t otaFlashHandler(httpd_req_t *request);
  static esp_err_t otaAbortHandler(httpd_req_t *request);

  bool authorizeOtaRequest(httpd_req_t *request);
  esp_err_t sendOtaSnapshot(httpd_req_t *request, const char *httpStatus);
  void buildOtaJson(char *buffer, size_t capacity,
                    const ControlGateSnapshot &control);
  void serviceOtaRollback(uint32_t now);
  static int otaReadChunk(void *context, uint8_t *buffer, size_t capacity);
  static bool otaTransferStillSafe(void *context);
  static void otaTransferProgress(void *context, uint32_t received,
                                  uint32_t expected);

  static esp_err_t sendJson(httpd_req_t *request, const char *status,
                            const char *json);
  static esp_err_t sendError(httpd_req_t *request, const char *status,
                             const char *error, const char *message);
  static bool readJsonBody(httpd_req_t *request,
                           char body[REQUEST_BODY_CAPACITY]);
  static bool requireJsonContentType(httpd_req_t *request);
  bool lockWorkBuf();
  void unlockWorkBuf();
  void unlockJsonBody();
  esp_err_t lockJsonBody(httpd_req_t *request, const char *invalidMessage);
  esp_err_t workBufBusy(httpd_req_t *request);
  bool requireActiveWebUiClient(httpd_req_t *request);
  void clearAdminUnlock();
  void grantAdminUnlock(const char *clientId, uint32_t now);
  void touchAdminUnlock();
  bool adminUnlockAllowed(httpd_req_t *request);
  bool requireAdminUnlock(httpd_req_t *request);
  bool webUiOverrideAllowed(httpd_req_t *request);
  bool webUiConfigurationAllowed(httpd_req_t *request,
                                 const ControlGateSnapshot &status);
  bool historyMutationAllowed(httpd_req_t *request,
                              const ControlGateSnapshot &status);
  void clearWebUiOverrideIfSafe(const ControlGateSnapshot &status);
  static const char *stateLabel(StopperState state);
  static const char *controlSourceName(ControlSource source);
  static const char *endReasonName(EndReason reason);
  static const char *staStateName(StaState state);
  static const char *wifiScanStateName(WifiScanState state);
};

}  // namespace shotstopper
