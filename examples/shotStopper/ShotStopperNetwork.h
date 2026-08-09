#pragma once

#include "ShotStopperDomain.h"
#include "ShotStopperPersistence.h"

#include <WiFi.h>
#include <esp_http_server.h>

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
  uint8_t apClients = 0;
  StaState staState = StaState::NOT_CONFIGURED;
  uint32_t windowRemainingMs = 0;
  char apIp[16] = "192.168.4.1";
  char staIp[16] = {};
};

struct NetworkBridgeCallbacks {
  void (*copyControlStatus)(ControlStatusSnapshot &output) = nullptr;
  bool (*enqueueWebCommand)(const WebCommand &command) = nullptr;
  size_t (*copyDebugEvents)(uint32_t afterSequence, DebugEvent *output,
                            size_t capacity) = nullptr;
  void (*addDebugEvent)(DebugCategory category, DebugCode code,
                        int32_t argument1, int32_t argument2) = nullptr;
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

 private:
  static constexpr uint32_t AP_WINDOW_MS = 180000;
  static constexpr uint32_t UI_GRACE_MS = 180000;
  static constexpr uint32_t WEB_PADDLE_HEARTBEAT_TIMEOUT_MS = 15000;
  static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t STA_RECONNECT_INTERVAL_MS = 10000;
  static constexpr uint32_t RESTART_DELAY_MS = 750;
  static constexpr size_t SESSION_COUNT = 2;
  static constexpr size_t TOKEN_BYTES = 16;
  static constexpr size_t TOKEN_HEX_CAPACITY = TOKEN_BYTES * 2 + 1;
  static constexpr size_t REQUEST_BODY_CAPACITY = 1025;
  static constexpr size_t LOG_BATCH_SIZE = 24;

  struct WebSession {
    bool active = false;
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
  uint32_t networkStartedAtMs_ = 0;
  uint32_t lastAuthenticatedAtMs_ = 0;
  uint32_t staConnectStartedAtMs_ = 0;
  uint32_t staReconnectAttemptAtMs_ = 0;
  uint32_t restartRequestedAtMs_ = 0;
  uint32_t loginWindowStartedAtMs_ = 0;
  uint8_t loginAttemptsInWindow_ = 0;

  static void taskEntry(void *parameter);
  void taskLoop();
  void service();
  void startNetwork();
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
  void processAcceptedCommand(const WebCommand &command);
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
  bool loginRateLimited(uint32_t now);
  static void randomHex(char output[TOKEN_HEX_CAPACITY]);

  static esp_err_t rootHandler(httpd_req_t *request);
  static esp_err_t loginHandler(httpd_req_t *request);
  static esp_err_t logoutHandler(httpd_req_t *request);
  static esp_err_t heartbeatHandler(httpd_req_t *request);
  static esp_err_t statusHandler(httpd_req_t *request);
  static esp_err_t logHandler(httpd_req_t *request);
  static esp_err_t configHandler(httpd_req_t *request);
  static esp_err_t resetCalibrationHandler(httpd_req_t *request);
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
