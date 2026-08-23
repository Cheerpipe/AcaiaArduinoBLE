#ifndef SHOT_STOPPER_HOST_STUBS_H
#define SHOT_STOPPER_HOST_STUBS_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t INPUT_PULLUP = 2;
constexpr uint8_t OUTPUT = 3;
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr int pdPASS = 1;
constexpr int tskIDLE_PRIORITY = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr int ESP_OK = 0;
constexpr int ESP_TIMER_TASK = 0;

using String = std::string;
using TickType_t = uint32_t;
using TaskHandle_t = void *;
using BaseType_t = int;
using portMUX_TYPE = int;

#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(ms) (ms)

// Match libraries/AcaiaArduinoBLE/AcaiaArduinoBLE.h so shotStopper.cpp can
// static_assert GAP connect vs the 5 s task watchdog in host builds.
#ifndef BLE_CONNECT_TIMEOUT_MS
#define BLE_CONNECT_TIMEOUT_MS 2000UL
#endif

inline uint32_t hostMillis = 0;
inline std::array<int, 64> hostPinLevel = {};
inline std::array<int, 64> hostPinMode = {};
inline size_t hostRelayOpenWrites = 0;
inline size_t hostRelayClosedWrites = 0;
inline int hostTrackedRelayPin = -1;
inline int hostTrackedRelayOpenLevel = LOW;
inline int hostTrackedRelayClosedLevel = HIGH;
inline bool hostTaskWatchdogOperationsSucceed = true;
inline bool hostTaskWatchdogConfigured = false;
inline size_t hostTaskWatchdogSubscriptions = 0;
inline size_t hostTaskWatchdogFeeds = 0;
inline bool hostGptimerCreateSucceeds = true;
inline bool hostGptimerArmSucceeds = true;
inline void (*hostCircuitArmBeforeCommitHook)() = nullptr;
inline uint32_t hostSafetyResetReasonCode = 1;
inline bool hostSafetyResetReasonUnsafe = false;
inline bool hostSafetyResetReasonPowerOn = true;

inline uint32_t millis() {
  return hostMillis;
}

inline void pinMode(uint8_t pin, uint8_t mode) {
  hostPinMode.at(pin) = mode;
}

inline int digitalRead(uint8_t pin) {
  return hostPinLevel.at(pin);
}

inline void digitalWrite(uint8_t pin, uint8_t level) {
  hostPinLevel.at(pin) = level;
  if (pin == hostTrackedRelayPin) {
    if (level == hostTrackedRelayOpenLevel) {
      ++hostRelayOpenWrites;
    } else if (level == hostTrackedRelayClosedLevel) {
      ++hostRelayClosedWrites;
    }
  }
}

inline size_t hostLedcAttachCalls = 0;
inline double hostLedcLastFreq = 0;

inline bool ledcAttach(uint8_t pin, double freq, uint8_t resolution) {
  (void)freq;
  (void)resolution;
  ++hostLedcAttachCalls;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  return true;
}

inline void ledcWriteTone(uint8_t pin, double freq) {
  hostLedcLastFreq = freq;
  digitalWrite(pin, freq > 0.0 ? HIGH : LOW);
}

inline bool hostCpuFrequencySetSucceeds = true;
inline uint32_t hostCpuFrequencyMhz = 80;

inline bool setCpuFrequencyMhz(uint32_t mhz) {
  if (!hostCpuFrequencySetSucceeds) {
    return false;
  }
  hostCpuFrequencyMhz = mhz;
  return true;
}

inline uint32_t getCpuFrequencyMhz() { return hostCpuFrequencyMhz; }

inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
inline void portENTER_CRITICAL_ISR(portMUX_TYPE *mux) { (void)mux; }
inline void portEXIT_CRITICAL_ISR(portMUX_TYPE *mux) { (void)mux; }

#define IRAM_ATTR

class HostSerial {
 public:
  std::string rx;
  std::string tx;
  size_t rxIndex = 0;

  void begin(unsigned long baud) { (void)baud; }

  void reset() {
    rx.clear();
    tx.clear();
    rxIndex = 0;
  }

  void inject(const char *text) {
    if (text != nullptr) {
      rx.append(text);
    }
  }

  int available() const {
    return rxIndex < rx.size() ? static_cast<int>(rx.size() - rxIndex) : 0;
  }

  int read() {
    if (rxIndex >= rx.size()) {
      return -1;
    }
    return static_cast<unsigned char>(rx[rxIndex++]);
  }

  template <typename T>
  void print(const T &value) {
    std::ostringstream stream;
    stream << value;
    tx += stream.str();
  }

  template <typename T>
  void println(const T &value) {
    print(value);
    tx += '\n';
  }

  void print(double value, int digits) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(digits < 0 ? 2 : digits);
    stream << value;
    tx += stream.str();
  }

  void print(float value, int digits) {
    print(static_cast<double>(value), digits);
  }

  void println(double value, int digits) {
    print(value, digits);
    tx += '\n';
  }

  void println(float value, int digits) {
    println(static_cast<double>(value), digits);
  }

  void println() { tx += '\n'; }
};

inline HostSerial Serial;

class HostEEPROM {
 public:
  HostEEPROM() { bytes.fill(0xFF); }

  bool begin(size_t size) {
    (void)size;
    return beginSucceeds;
  }

  uint8_t read(size_t address) const { return bytes.at(address); }
  void write(size_t address, uint8_t value) { bytes.at(address) = value; }
  bool commit() { return true; }

  bool beginSucceeds = true;
  std::array<uint8_t, 64> bytes;
};

inline HostEEPROM EEPROM;

class HostBLE {
 public:
  bool begin() { return beginSucceeds; }
  void poll() {}
  void setTimeout(unsigned long timeout) { configuredTimeoutMs = timeout; }

  bool beginSucceeds = true;
  unsigned long configuredTimeoutMs = 0;
};

inline HostBLE BLE;

constexpr size_t ACAIA_MAC_CAPACITY = 18;
constexpr size_t ACAIA_NAME_CAPACITY = 32;

enum class AcaiaDisconnectReason : uint8_t {
  NONE,
  USER_REQUEST,
  SCAN_START_FAILED,
  SCAN_TIMEOUT,
  CONNECT_FAILED,
  DISCOVERY_FAILED,
  UNSUPPORTED_SCALE,
  SUBSCRIBE_FAILED,
  INITIALIZATION_WRITE_FAILED,
  REMOTE_DISCONNECTED,
  FIRST_PACKET_TIMEOUT,
  PACKET_TIMEOUT,
  INVALID_PACKET_STREAM,
  COMMAND_WRITE_FAILED
};

class AcaiaArduinoBLE {
 public:
  explicit AcaiaArduinoBLE(bool debug) { (void)debug; }

  bool init(const char *mac = nullptr) {
    (void)mac;
    scanning = false;
    return connected;
  }
  bool startScan(const char *mac = nullptr, bool forceRestart = false) {
    lastForceRestart = forceRestart;
    const bool directed = mac != nullptr && mac[0] != '\0';
    if (scanning && !connected) {
      const bool sameFilter =
          directed ? (directedScan &&
                      strncmp(lastStartScanMac, mac,
                              sizeof(lastStartScanMac)) == 0)
                   : !directedScan;
      if (sameFilter && !forceRestart) {
        return true;
      }
    }
    ++startScanCalls;
    if (directed) {
      strncpy(lastStartScanMac, mac, sizeof(lastStartScanMac) - 1);
      lastStartScanMac[sizeof(lastStartScanMac) - 1] = '\0';
      directedScan = true;
    } else {
      lastStartScanMac[0] = '\0';
      directedScan = false;
    }
    if (!startScanSucceeds) {
      scanning = false;
      directedScan = false;
      disconnectReason = AcaiaDisconnectReason::SCAN_START_FAILED;
      return false;
    }
    if (connected) {
      scanning = false;
      directedScan = false;
      return false;
    }
    scanning = true;
    return true;
  }
  bool pollScan() {
    if (connected) {
      scanning = false;
      directedScan = false;
      connecting = false;
      return true;
    }
    if (connecting) {
      if (pollScanConnects) {
        connected = true;
        connecting = false;
        scanning = false;
        directedScan = false;
        return true;
      }
      return false;
    }
    if (!scanning) {
      return false;
    }
    if (pollScanConnects) {
      connecting = true;
      scanning = false;
      directedScan = false;
      // Match firmware stepped connect: first poll begins, later poll finishes.
      if (pollScanStepsToConnect <= 1) {
        connected = true;
        connecting = false;
        return true;
      }
      --pollScanStepsToConnect;
      return false;
    }
    return false;
  }
  bool isScanning() const { return scanning; }
  bool isConnecting() const { return connecting; }
  bool isDirectedScan() const { return scanning && directedScan; }
  bool takeSeenAdvertisement(char *macOut, size_t macCapacity, char *nameOut,
                             size_t nameCapacity) {
    if (!seenPending) {
      return false;
    }
    if (macOut != nullptr && macCapacity > 0) {
      strncpy(macOut, seenMac, macCapacity - 1);
      macOut[macCapacity - 1] = '\0';
    }
    if (nameOut != nullptr && nameCapacity > 0) {
      strncpy(nameOut, seenName, nameCapacity - 1);
      nameOut[nameCapacity - 1] = '\0';
    }
    seenPending = false;
    return true;
  }
  const char *address() const {
    return connected ? connectedAddress : "";
  }
  const char *localName() const {
    return connected ? connectedLocalName : "";
  }
  bool tare() {
    commandLog.push_back("tare");
    ++tareCalls;
    return runCommand(tareSucceeds);
  }
  bool startTimer() {
    commandLog.push_back("startTimer");
    ++startTimerCalls;
    return runCommand(startTimerSucceeds);
  }
  bool stopTimer() {
    commandLog.push_back("stopTimer");
    ++stopTimerCalls;
    return runCommand(stopTimerSucceeds);
  }
  bool resetTimer() {
    commandLog.push_back("resetTimer");
    ++resetTimerCalls;
    return runCommand(resetTimerSucceeds);
  }
  bool tareStartTimer() {
    commandLog.push_back("tareStartTimer");
    ++tareStartTimerCalls;
    if (!connected) {
      return false;
    }
    if (!tareStartTimerSucceeds) {
      return false;
    }
    return runCommand(true);
  }
  bool supportsTareStartTimer() const {
    return connected && tareStartTimerSupported;
  }
  bool supportsIndependentBeep() const {
    return connected && independentBeepSupported;
  }
  bool supportsCommandFeedback() const {
    return connected && commandFeedbackSupported;
  }
  bool beepWithoutStateChange() {
    commandLog.push_back("beepWithoutStateChange");
    ++beepCalls;
    return connected && independentBeepSupported && beepSucceeds;
  }
  bool setBeepLevel(uint8_t level) {
    commandLog.push_back(std::string("setBeepLevel:") + std::to_string(level));
    lastBeepLevel = level;
    ++setBeepLevelCalls;
    return connected && independentBeepSupported && beepSucceeds &&
           level <= 5;
  }
  bool heartbeat() {
    commandLog.push_back("heartbeat");
    ++heartbeatCalls;
    return runCommand(heartbeatSucceeds);
  }
  float getWeight() const { return weight; }
  bool hasTimer() const { return connected && timerValid; }
  uint32_t getTimerMs() const { return timerValid ? timerMs : 0; }
  uint32_t lastTimerAgeMs() const { return timerValid ? timerAgeMs : 0xffffffffUL; }
  bool heartbeatRequired() const { return heartbeatRequiredValue; }
  bool isConnected() const { return connected; }
  void disconnect() {
    connected = false;
    connecting = false;
    scanning = false;
    directedScan = false;
  }
  bool newWeightAvailable() {
    ++newWeightAvailableCalls;
    const bool available = newWeightAvailableValue;
    newWeightAvailableValue = false;
    if (disconnectWhenCheckingWeight) {
      connected = false;
    }
    return available;
  }
  const char* connectedProtocolName() const {
    return connected ? connectedProtocol : "none";
  }
  AcaiaDisconnectReason lastDisconnectReason() const {
    return disconnectReason;
  }
  const char* lastDisconnectReasonName() const {
    switch (disconnectReason) {
      case AcaiaDisconnectReason::NONE: return "none";
      case AcaiaDisconnectReason::SCAN_START_FAILED: return "scan start failed";
      case AcaiaDisconnectReason::SCAN_TIMEOUT: return "scan timeout";
      default: return "unknown";
    }
  }
  uint32_t rejectedPacketCount() const { return rejectedPackets; }
  uint32_t reconnectCount() const { return reconnects; }

  bool connected = false;
  bool scanning = false;
  bool connecting = false;
  bool directedScan = false;
  bool seenPending = false;
  bool startScanSucceeds = true;
  bool pollScanConnects = false;
  int pollScanStepsToConnect = 1;
  bool lastForceRestart = false;
  size_t startScanCalls = 0;
  char lastStartScanMac[ACAIA_MAC_CAPACITY] = {};
  char seenMac[ACAIA_MAC_CAPACITY] = {};
  char seenName[ACAIA_NAME_CAPACITY] = {};
  char connectedAddress[ACAIA_MAC_CAPACITY] = "01:02:03:04:05:06";
  char connectedLocalName[ACAIA_NAME_CAPACITY] = "BOOKOO";
  char connectedProtocol[20] = "bookoo_generic";
  bool tareSucceeds = true;
  bool startTimerSucceeds = true;
  bool stopTimerSucceeds = true;
  bool resetTimerSucceeds = true;
  bool tareStartTimerSucceeds = true;
  bool tareStartTimerSupported = true;
  bool independentBeepSupported = true;
  bool commandFeedbackSupported = true;
  bool beepSucceeds = true;
  bool heartbeatSucceeds = true;
  bool heartbeatRequiredValue = false;
  bool newWeightAvailableValue = false;
  bool disconnectWhenCheckingWeight = false;
  float weight = 0.0f;
  bool timerValid = false;
  uint32_t timerMs = 0;
  uint32_t timerAgeMs = 0;
  AcaiaDisconnectReason disconnectReason = AcaiaDisconnectReason::NONE;
  uint32_t rejectedPackets = 0;
  uint32_t reconnects = 0;
  size_t tareCalls = 0;
  size_t startTimerCalls = 0;
  size_t stopTimerCalls = 0;
  size_t resetTimerCalls = 0;
  size_t tareStartTimerCalls = 0;
  size_t beepCalls = 0;
  size_t setBeepLevelCalls = 0;
  uint8_t lastBeepLevel = 0;
  size_t heartbeatCalls = 0;
  size_t newWeightAvailableCalls = 0;
  std::vector<std::string> commandLog;

 private:
  bool runCommand(bool succeeds) {
    if (!connected || !succeeds) {
      connected = false;
      return false;
    }
    return true;
  }
};

struct HostQueue {
  HostQueue(size_t capacityValue, size_t itemSizeValue)
      : capacity(capacityValue), itemSize(itemSizeValue) {}

  size_t capacity;
  size_t itemSize;
  std::deque<std::vector<uint8_t>> items;
};

using QueueHandle_t = HostQueue *;

inline QueueHandle_t xQueueCreate(size_t capacity, size_t itemSize) {
  return new HostQueue(capacity, itemSize);
}

inline int xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
  (void)wait;
  if (queue == nullptr || queue->items.size() >= queue->capacity) {
    return pdFALSE;
  }
  std::vector<uint8_t> bytes(queue->itemSize);
  std::memcpy(bytes.data(), item, queue->itemSize);
  queue->items.push_back(std::move(bytes));
  return pdTRUE;
}

inline int xQueueSendToFront(QueueHandle_t queue, const void *item,
                             TickType_t wait) {
  (void)wait;
  if (queue == nullptr || queue->items.size() >= queue->capacity) {
    return pdFALSE;
  }
  std::vector<uint8_t> bytes(queue->itemSize);
  std::memcpy(bytes.data(), item, queue->itemSize);
  queue->items.push_front(std::move(bytes));
  return pdTRUE;
}

inline int xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
  (void)wait;
  if (queue == nullptr || queue->items.empty()) {
    return pdFALSE;
  }
  std::memcpy(item, queue->items.front().data(), queue->itemSize);
  queue->items.pop_front();
  return pdTRUE;
}

inline int xQueueOverwrite(QueueHandle_t queue, const void *item) {
  if (queue == nullptr || queue->capacity != 1) {
    return pdFALSE;
  }
  queue->items.clear();
  return xQueueSend(queue, item, 0);
}

inline void vQueueDelete(QueueHandle_t queue) { delete queue; }

inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t task) {
  (void)task;
  return 4096;
}

inline int xTaskCreate(void (*task)(void *), const char *name,
                       uint32_t stackDepth, void *parameter, int priority,
                       TaskHandle_t *handle) {
  (void)task;
  (void)name;
  (void)stackDepth;
  (void)parameter;
  (void)priority;
  if (handle != nullptr) {
    *handle = reinterpret_cast<TaskHandle_t>(1);
  }
  return pdPASS;
}

inline int xTaskCreatePinnedToCore(void (*task)(void *), const char *name,
                                   uint32_t stackDepth, void *parameter,
                                   int priority, TaskHandle_t *handle,
                                   BaseType_t core) {
  (void)core;
  return xTaskCreate(task, name, stackDepth, parameter, priority, handle);
}

struct esp_timer_create_args_t {
  void (*callback)(void *) = nullptr;
  void *arg = nullptr;
  int dispatch_method = ESP_TIMER_TASK;
  const char *name = nullptr;
};

struct HostEspTimer {
  void (*callback)(void *) = nullptr;
  void *arg = nullptr;
  bool active = false;
  uint64_t dueAtUs = 0;
};

using esp_timer_handle_t = HostEspTimer *;

inline bool hostEspTimerCreateSucceeds = true;
inline bool hostEspTimerStartSucceeds = true;

inline int esp_timer_create(const esp_timer_create_args_t *args,
                            esp_timer_handle_t *timer) {
  if (!hostEspTimerCreateSucceeds || args == nullptr || timer == nullptr ||
      args->callback == nullptr) {
    return -1;
  }
  *timer = new HostEspTimer{args->callback, args->arg, false, 0};
  return ESP_OK;
}

inline int esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeoutUs) {
  if (!hostEspTimerStartSucceeds || timer == nullptr) {
    return -1;
  }
  timer->active = true;
  timer->dueAtUs = static_cast<uint64_t>(hostMillis) * 1000ULL + timeoutUs;
  return ESP_OK;
}

inline int esp_timer_stop(esp_timer_handle_t timer) {
  if (timer == nullptr) {
    return -1;
  }
  timer->active = false;
  return ESP_OK;
}

inline void hostServiceEspTimer(esp_timer_handle_t timer) {
  if (timer == nullptr || !timer->active ||
      static_cast<uint64_t>(hostMillis) * 1000ULL < timer->dueAtUs) {
    return;
  }
  timer->active = false;
  timer->callback(timer->arg);
}

#endif
