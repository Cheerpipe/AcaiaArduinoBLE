#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperPersistence.h"
#include "ShotStopperShotLog.h"
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
  bool uiAuthenticated = false;
  bool wifiConfigured = false;
  bool staOpen = false;
  uint8_t apClients = 0;
  StaState staState = StaState::NOT_CONFIGURED;
  uint8_t staIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  uint8_t staConfigState = static_cast<uint8_t>(StaConfigState::CONFIRMED);
  uint32_t windowRemainingMs = 0;
  uint32_t confirmRemainingMs = 0;
  uint32_t taskAgeMs = 0;
  uint32_t taskStackMinWords = 0;
  uint32_t startupFailures = 0;
  uint32_t lastCommandRequestId = 0;
  CommandResultState lastCommandState = CommandResultState::NONE;
  char apIp[16] = "192.168.4.1";
  char staIp[16] = {};
  char staSsid[WIFI_SSID_CAPACITY] = {};
  char configuredIp[16] = {};
  char configuredNetmask[16] = {};
  char configuredGateway[16] = {};
  char configuredDns1[16] = {};
  char configuredDns2[16] = {};
};

struct NetworkBridgeCallbacks {
  void (*copyControlStatus)(ControlStatusSnapshot &output) = nullptr;
  bool (*enqueueWebCommand)(const WebCommand &command) = nullptr;
  size_t (*copyDebugEvents)(uint32_t afterSequence, DebugEvent *output,
                            size_t capacity) = nullptr;
  void (*addDebugEvent)(DebugCategory category, DebugCode code,
                        int32_t argument1, int32_t argument2) = nullptr;
  void (*reportTaskWatchdogFault)() = nullptr;
  void (*requestSafeRestart)() = nullptr;
  size_t (*copyShotRecords)(ShotLogRecord *output, size_t capacity) = nullptr;
  bool (*deleteShotRecord)(uint32_t id) = nullptr;
  bool (*clearShotLog)() = nullptr;
  // Keeps NVS Wi-Fi/runtime saves from overwriting a newer preferred scale MAC.
  void (*copyPreferredScaleMac)(char *out, size_t capacity) = nullptr;
  void (*copyPreferredScaleName)(char *out, size_t capacity) = nullptr;
  void (*copyPresetBank)(ShotPresetBank *out) = nullptr;
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

  private:
  void mergePreferredScaleMac(PersistedSettings &settings);
  static constexpr uint32_t AP_WINDOW_MS = 180000;
  static constexpr uint32_t UI_GRACE_MS = 180000;
  static constexpr uint32_t WEB_PADDLE_HEARTBEAT_TIMEOUT_MS = 15000;
  static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t STA_CONFIRM_TIMEOUT_MS = 180000;
  static constexpr uint32_t STA_RECONNECT_INTERVAL_MS = 10000;
  static constexpr uint32_t RESTART_DELAY_MS = 750;
  static constexpr uint32_t NETWORK_RETRY_MIN_MS = 1000;
  static constexpr uint32_t NETWORK_RETRY_MAX_MS = 30000;
  static constexpr uint32_t COMMAND_RETRY_MIN_MS = 250;
  static constexpr uint32_t MAINTENANCE_PUBLICATION_TIMEOUT_MS = 2000;
  static constexpr uint32_t HTTP_RETRY_MS = 1000;
  static constexpr uint32_t HEALTH_TELEMETRY_INTERVAL_MS = 5000;
  static constexpr uint8_t COMMAND_MAX_ATTEMPTS = 5;
  static constexpr size_t SESSION_COUNT = 2;
  static constexpr size_t TOKEN_BYTES = 16;
  static constexpr size_t TOKEN_HEX_CAPACITY = TOKEN_BYTES * 2 + 1;
  static constexpr size_t REQUEST_BODY_CAPACITY = 1025;
  static constexpr size_t LOG_BATCH_SIZE = 48;

  struct WebSession {
    bool active = false;
    uint32_t id = 0;
    uint32_t lastHeartbeatMs = 0;
    char token[TOKEN_HEX_CAPACITY] = {};
    char csrf[TOKEN_HEX_CAPACITY] = {};
  };

  static ShotStopperNetwork *instance_;

  PersistedSettings settings_ = {};
  NetworkBridgeCallbacks callbacks_ = {};
  QueueHandle_t acceptedCommandQueue_ = nullptr;
  TaskHandle_t taskHandle_ = nullptr;
  httpd_handle_t server_ = nullptr;
  portMUX_TYPE dataMux_ = portMUX_INITIALIZER_UNLOCKED;
  WebSession sessions_[SESSION_COUNT] = {};
  NetworkStatusSnapshot status_ = {};
  WifiScanSnapshot scan_ = {};
  bool startupComplete_ = false;
  bool staEverConnected_ = false;
  bool scanRequested_ = false;
  bool everAuthenticated_ = false;
  bool networkShutdownPending_ = false;
  bool restartPending_ = false;
  bool apRestartPending_ = false;
  bool heartbeatStopSent_ = false;
  bool acceptedCommandPending_ = false;
  bool completionPending_ = false;
  bool staConfirmArmed_ = false;
  bool pendingConfirmRequest_ = false;
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
  uint32_t nextSessionId_ = 1;
  uint32_t nextControlLeaseId_ = 1;
  uint32_t scanMaintenanceLeaseId_ = 0;
  uint32_t scanRequestId_ = 0;
  uint32_t networkStartedAtMs_ = 0;
  uint32_t lastAuthenticatedAtMs_ = 0;
  uint32_t staConnectStartedAtMs_ = 0;
  uint32_t staReconnectAttemptAtMs_ = 0;
  uint32_t staConfirmDeadlineMs_ = 0;
  uint32_t restartRequestedAtMs_ = 0;
  uint32_t loginWindowStartedAtMs_ = 0;
  uint8_t loginAttemptsInWindow_ = 0;
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
  bool startFallbackAccessPoint(uint32_t now);
  void stopNetwork();
  bool startHttpServer();
  void stopHttpServer();
  void serviceStaState(uint32_t now);
  void serviceWifiScan(uint32_t now);
  void finishWifiScan(int16_t resultCount, uint32_t now);
  void serviceSessions(uint32_t now);
  void processAcceptedCommands();
  bool processAcceptedCommand(const WebCommand &command);
  void publishConfiguredAddressStatus();
  void armPendingConfirmWindow(uint32_t now);
  void clearPendingConfirmWindow();
  void requestPendingNetworkConfirm();
  bool confirmPendingNetwork(const char *reason);
  bool revertPendingNetwork(uint32_t now, const char *reason);
  bool controlAllowsNetworkMutation(ControlStatusSnapshot *copy = nullptr);
  void log(DebugCategory category, DebugCode code, int32_t argument1 = 0,
           int32_t argument2 = 0);

  PersistedSettings settingsCopy();
  bool authenticate(httpd_req_t *request, bool requireCsrf,
                    size_t *sessionIndex = nullptr);
  bool createSession(char token[TOKEN_HEX_CAPACITY],
                     char csrf[TOKEN_HEX_CAPACITY]);
  void invalidateSession(size_t index, DebugCode code);
  void invalidateAllSessions();
  void requestStopForSession(uint32_t webSessionId);
  bool enqueueMaintenanceCompletion(const WebCommand &command,
                                    bool succeeded,
                                    CommandResultState failureState =
                                        CommandResultState::FAILED);
  void recordCommandResult(uint32_t requestId, CommandResultState state);
  esp_err_t sendAccepted(httpd_req_t *request, uint32_t requestId,
                         const char *extraJson = nullptr);
  uint32_t allocateRequestId();
  uint32_t allocateSessionId();
  uint32_t allocateControlLeaseId();
  bool loginRateLimited(uint32_t now);
  static void randomHex(char output[TOKEN_HEX_CAPACITY]);

  static esp_err_t rootHandler(httpd_req_t *request);
  static esp_err_t cssHandler(httpd_req_t *request);
  static esp_err_t logoHandler(httpd_req_t *request);
  static esp_err_t notFoundHandler(httpd_req_t *request, httpd_err_code_t error);
  static esp_err_t loginHandler(httpd_req_t *request);
  static esp_err_t logoutHandler(httpd_req_t *request);
  static esp_err_t heartbeatHandler(httpd_req_t *request);
  static esp_err_t statusHandler(httpd_req_t *request);
  static esp_err_t logHandler(httpd_req_t *request);
  static esp_err_t shotsHandler(httpd_req_t *request);
  static esp_err_t shotsClearHandler(httpd_req_t *request);
  static esp_err_t shotsDeleteHandler(httpd_req_t *request);
  static esp_err_t timeSyncHandler(httpd_req_t *request);
  static esp_err_t configHandler(httpd_req_t *request);
  static esp_err_t preferredScaleClearHandler(httpd_req_t *request);
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
  static esp_err_t apPasswordHandler(httpd_req_t *request);

  static esp_err_t sendJson(httpd_req_t *request, const char *status,
                            const char *json);
  static esp_err_t sendError(httpd_req_t *request, const char *status,
                             const char *error, const char *message);
  static bool readJsonBody(httpd_req_t *request,
                           char body[REQUEST_BODY_CAPACITY]);
  static bool requireJsonContentType(httpd_req_t *request);
  static const char *stateLabel(StopperState state);
  static const char *controlSourceName(ControlSource source);
  static const char *endReasonName(EndReason reason);
  static const char *staStateName(StaState state);
  static const char *wifiScanStateName(WifiScanState state);
};

}  // namespace shotstopper
