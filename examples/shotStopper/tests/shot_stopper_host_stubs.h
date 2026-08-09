#ifndef SHOT_STOPPER_HOST_STUBS_H
#define SHOT_STOPPER_HOST_STUBS_H

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
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
using portMUX_TYPE = int;

#define portMUX_INITIALIZER_UNLOCKED 0
#define pdMS_TO_TICKS(ms) (ms)

inline uint32_t hostMillis = 0;
inline std::array<int, 64> hostPinLevel = {};
inline std::array<int, 64> hostPinMode = {};
inline size_t hostRelayOpenWrites = 0;
inline size_t hostRelayClosedWrites = 0;
inline int hostTrackedRelayPin = -1;
inline int hostTrackedRelayOpenLevel = LOW;
inline int hostTrackedRelayClosedLevel = HIGH;

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

inline void analogWrite(uint8_t pin, int value) {
  (void)pin;
  (void)value;
}

inline void setCpuFrequencyMhz(uint32_t mhz) {
  (void)mhz;
}

inline void enableLoopWDT() {}
inline void vTaskDelay(TickType_t ticks) { (void)ticks; }
inline void portENTER_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
inline void portEXIT_CRITICAL(portMUX_TYPE *mux) { (void)mux; }

class HostSerial {
 public:
  void begin(unsigned long baud) { (void)baud; }

  template <typename T>
  void print(const T &value) {
    (void)value;
  }

  template <typename T>
  void println(const T &value) {
    (void)value;
  }

  void println() {}
};

inline HostSerial Serial;

class HostEEPROM {
 public:
  HostEEPROM() { bytes.fill(0xFF); }

  bool begin(size_t size) {
    (void)size;
    return true;
  }

  uint8_t read(size_t address) const { return bytes.at(address); }
  void write(size_t address, uint8_t value) { bytes.at(address) = value; }
  bool commit() { return true; }

  std::array<uint8_t, 64> bytes;
};

inline HostEEPROM EEPROM;

class HostBLE {
 public:
  bool begin() { return true; }
  void poll() {}
};

inline HostBLE BLE;

class AcaiaArduinoBLE {
 public:
  explicit AcaiaArduinoBLE(bool debug) { (void)debug; }

  bool init(String mac = "") {
    (void)mac;
    return connected;
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
    return runCommand(tareStartTimerSucceeds);
  }
  bool supportsIndependentBeep() const {
    return connected && independentBeepSupported;
  }
  bool beepWithoutStateChange() {
    commandLog.push_back("beepWithoutStateChange");
    ++beepCalls;
    return connected && independentBeepSupported && beepSucceeds;
  }
  bool heartbeat() {
    commandLog.push_back("heartbeat");
    ++heartbeatCalls;
    return runCommand(heartbeatSucceeds);
  }
  float getWeight() const { return weight; }
  bool heartbeatRequired() const { return heartbeatRequiredValue; }
  bool isConnected() const { return connected; }
  bool newWeightAvailable() {
    ++newWeightAvailableCalls;
    const bool available = newWeightAvailableValue;
    newWeightAvailableValue = false;
    if (disconnectWhenCheckingWeight) {
      connected = false;
    }
    return available;
  }

  bool connected = false;
  bool tareSucceeds = true;
  bool startTimerSucceeds = true;
  bool stopTimerSucceeds = true;
  bool resetTimerSucceeds = true;
  bool tareStartTimerSucceeds = true;
  bool independentBeepSupported = true;
  bool beepSucceeds = true;
  bool heartbeatSucceeds = true;
  bool heartbeatRequiredValue = false;
  bool newWeightAvailableValue = false;
  bool disconnectWhenCheckingWeight = false;
  float weight = 0.0f;
  size_t tareCalls = 0;
  size_t startTimerCalls = 0;
  size_t stopTimerCalls = 0;
  size_t resetTimerCalls = 0;
  size_t tareStartTimerCalls = 0;
  size_t beepCalls = 0;
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

inline int xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
  (void)wait;
  if (queue == nullptr || queue->items.empty()) {
    return pdFALSE;
  }
  std::memcpy(item, queue->items.front().data(), queue->itemSize);
  queue->items.pop_front();
  return pdTRUE;
}

inline void vQueueDelete(QueueHandle_t queue) { delete queue; }

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
