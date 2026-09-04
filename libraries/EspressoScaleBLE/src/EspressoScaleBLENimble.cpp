/*
  Native ESP-IDF NimBLE implementation of the EspressoScaleBLE facade.
*/
#include "EspressoScaleBLE.h"

#include "ShotStopperBleRuntime.h"
#include "nimble/NimbleAdvertisement.h"
#include "nimble/NimbleResilience.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "nimble/ble.h"
#include "os/os_mbuf.h"

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Shot Stopper retains scale diagnostics in its bounded WebUI log. Keep the
// bridge weak so this library remains reusable by standalone IDF projects.
extern "C" void shotStopperScaleLog(uint8_t severity, const char *message)
    __attribute__((weak));

namespace {

constexpr char kTag[] = "scale.nimble";
constexpr uint16_t kInvalidHandle = 0xffff;
constexpr size_t kCandidateCount = 8;
constexpr size_t kServiceCount = 24;
constexpr size_t kEventCount = 12;
constexpr size_t kCriticalEventCount = 6;
constexpr size_t kRxFrameCount = 16;
constexpr size_t kProtocolCapacity = 11;
constexpr uint32_t kScanCancelTimeoutMs = 1000;
constexpr uint32_t kUnsupportedCooldownMs = 60000;
constexpr uint32_t kGattFailureCooldownMs = 2000;

void scaleLogDebug(const char *format, ...) {
  if (format == nullptr) {
    return;
  }
  char message[128] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (shotStopperScaleLog != nullptr) {
    shotStopperScaleLog(0, message);
  } else {
    ESP_LOGD(kTag, "%s", message);
  }
}

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t elapsedMs(uint32_t since) {
  return static_cast<uint32_t>(nowMs() - since);
}

bool addressEqual(const uint8_t left[6], const uint8_t right[6]) {
  return memcmp(left, right, 6) == 0;
}

void formatAddress(const uint8_t address[6], char *output, size_t capacity) {
  if (output == nullptr || capacity < SCALE_MAC_CAPACITY) {
    return;
  }
  snprintf(output, capacity, "%02X:%02X:%02X:%02X:%02X:%02X", address[5],
           address[4], address[3], address[2], address[1], address[0]);
}

int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parseAddress(const char *text, uint8_t output[6]) {
  if (text == nullptr || strlen(text) != 17) {
    return false;
  }
  uint8_t displayOrder[6] = {};
  for (size_t index = 0; index < 6; ++index) {
    const size_t offset = index * 3;
    const int high = hexNibble(text[offset]);
    const int low = hexNibble(text[offset + 1]);
    if (high < 0 || low < 0 || (index < 5 && text[offset + 2] != ':')) {
      return false;
    }
    displayOrder[index] = static_cast<uint8_t>((high << 4) | low);
  }
  for (size_t index = 0; index < 6; ++index) {
    output[5 - index] = displayOrder[index];
  }
  return true;
}

const char *disconnectReasonName(ScaleDisconnectReason reason) {
  switch (reason) {
    case ScaleDisconnectReason::NONE: return "none";
    case ScaleDisconnectReason::USER_REQUEST: return "user request";
    case ScaleDisconnectReason::SCAN_START_FAILED: return "scan start failed";
    case ScaleDisconnectReason::SCAN_TIMEOUT: return "scan timeout";
    case ScaleDisconnectReason::CONNECT_FAILED: return "connect failed";
    case ScaleDisconnectReason::DISCOVERY_FAILED: return "discovery failed";
    case ScaleDisconnectReason::UNSUPPORTED_SCALE: return "unsupported scale";
    case ScaleDisconnectReason::SUBSCRIBE_FAILED: return "subscribe failed";
    case ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED:
      return "initialization write failed";
    case ScaleDisconnectReason::REMOTE_DISCONNECTED:
      return "remote disconnected";
    case ScaleDisconnectReason::FIRST_PACKET_TIMEOUT:
      return "first packet timeout";
    case ScaleDisconnectReason::PACKET_TIMEOUT: return "packet timeout";
    case ScaleDisconnectReason::INVALID_PACKET_STREAM:
      return "invalid packet stream";
    case ScaleDisconnectReason::COMMAND_WRITE_FAILED:
      return "command write failed";
    case ScaleDisconnectReason::SUPERVISION_TIMEOUT:
      return "supervision timeout";
    case ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH:
      return "connection failed to be established";
    case ScaleDisconnectReason::RX_QUEUE_OVERFLOW:
      return "RX queue overflow";
    case ScaleDisconnectReason::EVENT_QUEUE_OVERFLOW:
      return "event queue overflow";
    case ScaleDisconnectReason::HOST_RESET: return "host reset";
    case ScaleDisconnectReason::OPERATION_TIMEOUT:
      return "operation timeout";
    case ScaleDisconnectReason::MBUF_ALLOCATION_FAILED:
      return "mbuf allocation failed";
  }
  return "unknown";
}

ScaleDisconnectReason mapRawDisconnectReason(int status) {
  if (status == 0x08 || status == BLE_HS_HCI_ERR(0x08)) {
    return ScaleDisconnectReason::SUPERVISION_TIMEOUT;
  }
  if (status == 0x3e || status == BLE_HS_HCI_ERR(0x3e)) {
    return ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH;
  }
  return ScaleDisconnectReason::REMOTE_DISCONNECTED;
}

class NimbleScaleClient {
 public:
  explicit NimbleScaleClient(bool debug) : debug_(debug) {
    if (activeClient_ == nullptr) {
      activeClient_ = this;
      callbackOwner_ = true;
    }
  }

  ~NimbleScaleClient() {
    finishLink(true, ScaleDisconnectReason::USER_REQUEST, 0);
    if (callbackOwner_ && activeClient_ == this) {
      activeClient_ = nullptr;
    }
  }

  bool startScan(const char *mac, bool forceRestart, uint16_t interval,
                 uint16_t window, bool addressScan) {
    service();
    if (!callbackOwner_) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_EBUSY;
      return false;
    }
    if (scaleProtocolCount() > kProtocolCapacity) {
      lastReason_ = ScaleDisconnectReason::UNSUPPORTED_SCALE;
      lastRawStatus_ = BLE_HS_EINVAL;
      return false;
    }
    if (!shotStopperBleRuntimeReady()) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_ENOTSYNCED;
      return false;
    }
    if (window == 0 || window > interval) {
      interval = BLE_SCAN_NORMAL_INTERVAL;
      window = BLE_SCAN_NORMAL_WINDOW;
    }

    uint8_t parsedFilter[6] = {};
    const bool filtered = mac != nullptr && mac[0] != '\0';
    if (filtered && !parseAddress(mac, parsedFilter)) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_EINVAL;
      return false;
    }
    const bool useAddressScan = filtered && addressScan;
    if ((state_ == State::Scanning || state_ == State::Backoff) &&
        !forceRestart &&
        scanInterval_ == interval && scanWindow_ == window &&
        scanAddressFilter_ == useAddressScan &&
        filterPresent_ == filtered &&
        (!filtered || addressEqual(filterAddress_, parsedFilter))) {
      return true;
    }

    if (state_ != State::Idle) {
      finishLink(true, ScaleDisconnectReason::NONE, 0);
    }
    scanInterval_ = interval;
    scanWindow_ = window;
    filterPresent_ = filtered;
    scanAddressFilter_ = useAddressScan;
    if (filtered) {
      memcpy(filterAddress_, parsedFilter, sizeof(filterAddress_));
    } else {
      memset(filterAddress_, 0, sizeof(filterAddress_));
    }
    if (forceRestart) {
      backoff_.reset();
    }
    if (backoff_.active(nowMs())) {
      enterState(State::Backoff, backoff_.remainingMs(nowMs()));
      return true;
    }
    return beginConfiguredScan(forceRestart);
  }

  bool poll() {
    service();
    return state_ == State::Ready;
  }

  bool isScanning() const {
    return state_ == State::Scanning || state_ == State::Backoff;
  }

  bool isConnecting() const {
    return state_ == State::CancelPending || state_ == State::Connecting ||
           state_ == State::DiscoveringServices ||
           state_ == State::DiscoveringCharacteristics ||
           state_ == State::DiscoveringDescriptors ||
           state_ == State::Subscribing || state_ == State::Initializing;
  }

  void disconnect() {
    finishLink(true, ScaleDisconnectReason::USER_REQUEST, 0);
  }

  bool isConnected() {
    service();
    return state_ == State::Ready;
  }

  bool isLinkUp() const { return state_ == State::Ready; }

  bool newWeightAvailable() {
    service();
    if (state_ != State::Ready || protocol_ == nullptr) {
      return false;
    }
    if (!hasValidPacket_ && elapsedMs(connectedAt_) >= FIRST_PACKET_TIMEOUT_MS) {
      finishLink(true, ScaleDisconnectReason::FIRST_PACKET_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return false;
    }
    if (hasValidPacket_ && elapsedMs(lastPacket_) >= maxPacketPeriodMs()) {
      finishLink(true, ScaleDisconnectReason::PACKET_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return false;
    }

    RxFrame frame = {};
    while (popRx(frame)) {
      if (!supportedPacketLength(frame.length)) {
        rejectPacket();
        continue;
      }
      float weight = 0.0f;
      uint32_t timerMs = 0;
      const bool hasWeight = protocol_->parseWeight != nullptr &&
                             protocol_->parseWeight(frame.data, frame.length,
                                                    &weight);
      const bool hasTimer = protocol_->parseTimer != nullptr &&
                            protocol_->parseTimer(frame.data, frame.length,
                                                  &timerMs);
      if (!hasWeight && !hasTimer) {
        rejectPacket();
        continue;
      }
      if (hasValidPacket_) {
        packetPeriod_ = frame.receivedAtMs - lastPacket_;
      }
      lastPacket_ = frame.receivedAtMs;
      hasValidPacket_ = true;
      consecutiveRejectedPackets_ = 0;
      if (hasTimer) {
        currentTimerMs_ = timerMs;
        lastTimerPacket_ = frame.receivedAtMs;
        hasTimer_ = true;
      }
      if (hasWeight) {
        currentWeight_ = weight;
        return true;
      }
    }
    return false;
  }

  ScaleCommandResult writeOp(ScaleOp op, uint8_t arg = 0) {
    if (!isConnected()) {
      return ScaleCommandResult::NotConnected;
    }
    if (protocol_ == nullptr || protocol_->encodeCommand == nullptr) {
      return ScaleCommandResult::Unsupported;
    }
    uint8_t command[SCALE_MAX_COMMAND_LENGTH] = {};
    int length = 0;
    if (!protocol_->encodeCommand(op, arg, command, &length) || length <= 0 ||
        length > SCALE_MAX_COMMAND_LENGTH) {
      return ScaleCommandResult::Unsupported;
    }
    return writeCommand(command, static_cast<uint16_t>(length));
  }

  ScaleFeatureSet features() const {
    return state_ == State::Ready && protocol_ != nullptr
               ? protocol_->features
               : scaleFeatureSetNone();
  }

  bool heartbeatRequired() const {
    if (!features().has(ScaleFeatureHeartbeat)) {
      return false;
    }
    const uint16_t period = protocol_->features.heartbeatPeriodMs != 0
                                ? protocol_->features.heartbeatPeriodMs
                                : HEARTBEAT_PERIOD_MS;
    return elapsedMs(lastHeartbeat_) >= period;
  }

  void noteHeartbeat() { lastHeartbeat_ = nowMs(); }

  bool hasTimer() const { return state_ == State::Ready && hasTimer_; }
  uint32_t timerMs() const { return hasTimer_ ? currentTimerMs_ : 0; }

  uint32_t timerAgeMs() const {
    return hasTimer_ ? elapsedMs(lastTimerPacket_) : 0xffffffffUL;
  }

  const char *protocolName() const {
    return state_ == State::Ready && protocol_ != nullptr ? protocol_->id
                                                          : "none";
  }

  const char *address() const { return identityPresent_ ? address_ : ""; }
  const char *name() const { return identityPresent_ ? name_ : ""; }
  bool directedScan() const {
    return (state_ == State::Scanning || state_ == State::Backoff) &&
           filterPresent_;
  }

  bool takeSeenAdvertisement(char *macOut, size_t macCapacity, char *nameOut,
                             size_t nameCapacity) {
    portENTER_CRITICAL(&mux_);
    const bool pending = seenPending_;
    if (pending) {
      if (macOut != nullptr && macCapacity != 0) {
        strncpy(macOut, seenAddress_, macCapacity - 1);
        macOut[macCapacity - 1] = '\0';
      }
      if (nameOut != nullptr && nameCapacity != 0) {
        strncpy(nameOut, seenName_, nameCapacity - 1);
        nameOut[nameCapacity - 1] = '\0';
      }
      seenPending_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    return pending;
  }

  ScaleDisconnectReason lastReason() const { return lastReason_; }
  int32_t lastRawStatus() const { return lastRawStatus_; }
  uint8_t connectAttempts() const { return connectAttempts_; }
  uint8_t stateId() const { return static_cast<uint8_t>(state_); }
  uint32_t lastPacketAgeMs() const {
    return hasValidPacket_ ? elapsedMs(lastPacket_) : 0xffffffffUL;
  }
  uint32_t rejectedPackets() const { return rejectedPackets_; }
  uint32_t reconnects() const { return reconnects_; }
  ScaleBleTimingSnapshot timing() const {
    portENTER_CRITICAL(&mux_);
    const ScaleBleTimingSnapshot snapshot = timing_;
    portEXIT_CRITICAL(&mux_);
    return snapshot;
  }
  float weight() const { return currentWeight_; }

  int rssi() const {
    if (state_ != State::Ready || connectionHandle_ == kInvalidHandle) {
      return SCALE_LINK_RSSI_UNAVAILABLE;
    }
    int8_t value = 0;
    return ble_gap_conn_rssi(connectionHandle_, &value) == 0
               ? static_cast<int>(value)
               : SCALE_LINK_RSSI_UNAVAILABLE;
  }

  uint16_t rxHighWater() const { return rxHighWater_; }
  uint32_t rxDrops() const { return rxDrops_; }

  ScaleBleBackendHealth health() const {
    ScaleBleBackendHealth result = {};
    portENTER_CRITICAL(&mux_);
    result.generation = generation_;
    result.operationId = operationId_;
    result.stateAgeMs = elapsedMs(stateEnteredAtMs_);
    result.advertisementsSeen = advertisementsSeen_;
    result.compatibleAdvertisements = compatibleAdvertisements_;
    result.discardedAdvertisements = discardedAdvertisements_;
    result.malformedAdvertisements = malformedAdvertisements_;
    result.negativeCacheHits = negativeCacheHits_;
    result.negativeCacheInsertions = negativeCacheInsertions_;
    result.scanStarts = scanStarts_;
    result.scanCancels = scanCancels_;
    result.scanRestarts = scanRestarts_;
    result.connectAttempts = connectAttemptsTotal_;
    result.connectionFailures = connectionFailures_;
    result.discoveryFailures = discoveryFailures_;
    result.subscriptionFailures = subscriptionFailures_;
    result.writeFailures = writeFailures_;
    result.staleCallbacks = staleCallbacks_;
    result.criticalEventDrops = criticalEvents_.drops();
    result.controlEventDrops = controlEvents_.drops();
    result.rxDrops = rxDrops_;
    result.mbufFailures = mbufFailures_;
    result.cleanupCount = cleanupCount_;
    result.duplicateCleanups = duplicateCleanups_;
    result.backoffCount = backoffCount_;
    result.lastAdvertisementToConnectMs = lastAdvertisementToConnectMs_;
    result.lastAdvertisementToReadyMs = lastAdvertisementToReadyMs_;
    result.criticalEventHighWater =
        static_cast<uint16_t>(criticalEvents_.highWater());
    result.controlEventHighWater =
        static_cast<uint16_t>(controlEvents_.highWater());
    result.rxHighWater = rxHighWater_;
    result.state = static_cast<uint8_t>(state_);
    result.backoffFailures = backoff_.failureCount();
    portEXIT_CRITICAL(&mux_);
    portENTER_CRITICAL(&advertMux_);
    result.negativeCacheEntries = static_cast<uint8_t>(
        negativeCache_.activeCount(nowMs()));
    portEXIT_CRITICAL(&advertMux_);
    return result;
  }

 private:
  enum class State : uint8_t {
    Idle,
    Scanning,
    CancelPending,
    Connecting,
    DiscoveringServices,
    DiscoveringCharacteristics,
    DiscoveringDescriptors,
    Subscribing,
    Initializing,
    Ready,
    Backoff
  };

  enum class EventType : uint8_t {
    Candidate,
    ScanComplete,
    ConnectComplete,
    Disconnected,
    ServicesComplete,
    CharacteristicsComplete,
    DescriptorsComplete,
    WriteComplete
  };

  enum class WritePurpose : uint8_t { None, Subscribe, Initialize, Command };

  enum class CallbackDomain : uint8_t { Scan, Link, Gatt };

  struct Event {
    EventType type;
    uint32_t generation;
    uint32_t operationId;
    int32_t status;
    uint16_t connectionHandle;
    ble_addr_t address;
    char name[SCALE_NAME_CAPACITY];
  };

  struct Candidate {
    ble_addr_t address;
    NimbleAdvertisementData advertisement;
    uint32_t sequence;
    bool used;
    bool connectable;
  };

  struct ServiceRange {
    uint16_t start;
    uint16_t end;
  };

  struct ProtocolHandles {
    uint16_t read;
    uint16_t readEnd;
    uint16_t write;
    uint8_t readProperties;
    uint8_t writeProperties;
    uint8_t readService;
  };

  struct RxFrame {
    uint32_t generation;
    uint32_t operationId;
    uint32_t receivedAtMs;
    uint8_t length;
    uint8_t data[MAX_BLE_PACKET_LENGTH];
  };

  static void *callbackArg(uint32_t operationId) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(operationId));
  }

  void noteStaleCallback() {
    portENTER_CRITICAL(&mux_);
    ++staleCallbacks_;
    portEXIT_CRITICAL(&mux_);
  }

  bool callbackMatches(uint32_t actual, CallbackDomain domain) {
    portENTER_CRITICAL(&mux_);
    const uint32_t expected =
        domain == CallbackDomain::Scan
            ? scanOperationId_
            : (domain == CallbackDomain::Link ? linkOperationId_
                                               : gattOperationId_);
    const bool matches = actual != 0 && actual == expected;
    if (!matches) {
      ++staleCallbacks_;
    }
    portEXIT_CRITICAL(&mux_);
    return matches;
  }

  static int gapCallback(ble_gap_event *event, void *arg) {
    return activeClient_ == nullptr
               ? 0
               : activeClient_->onGapEvent(
                     event, static_cast<uint32_t>(
                                reinterpret_cast<uintptr_t>(arg)));
  }

  static int serviceCallback(uint16_t connectionHandle,
                             const ble_gatt_error *error,
                             const ble_gatt_svc *service, void *arg) {
    return activeClient_ == nullptr
               ? 0
               : activeClient_->onService(
                     connectionHandle, error, service,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int characteristicCallback(uint16_t connectionHandle,
                                    const ble_gatt_error *error,
                                    const ble_gatt_chr *characteristic,
                                    void *arg) {
    return activeClient_ == nullptr
               ? 0
               : activeClient_->onCharacteristic(
                     connectionHandle, error, characteristic,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int descriptorCallback(uint16_t connectionHandle,
                                const ble_gatt_error *error,
                                uint16_t characteristicValueHandle,
                                const ble_gatt_dsc *descriptor, void *arg) {
    return activeClient_ == nullptr
               ? 0
               : activeClient_->onDescriptor(
                     connectionHandle, error, characteristicValueHandle,
                     descriptor,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int writeCallback(uint16_t connectionHandle,
                           const ble_gatt_error *error, ble_gatt_attr *,
                           void *arg) {
    return activeClient_ == nullptr
               ? 0
               : activeClient_->onWrite(
                     connectionHandle, error,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  int onGapEvent(ble_gap_event *event, uint32_t operationId) {
    if (event == nullptr) {
      return 0;
    }
    switch (event->type) {
      case BLE_GAP_EVENT_DISC:
        if (callbackMatches(operationId, CallbackDomain::Scan)) {
          onAdvertisement(event->disc, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_DISC_COMPLETE:
        if (callbackMatches(operationId, CallbackDomain::Scan)) {
          pushCriticalEvent(EventType::ScanComplete,
                            event->disc_complete.reason, kInvalidHandle,
                            operationId);
        }
        return 0;
      case BLE_GAP_EVENT_CONNECT:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          pushCriticalEvent(EventType::ConnectComplete, event->connect.status,
                            event->connect.conn_handle, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_DISCONNECT:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          pushCriticalEvent(EventType::Disconnected, event->disconnect.reason,
                            event->disconnect.conn.conn_handle, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_NOTIFY_RX:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          onNotification(event->notify_rx.conn_handle,
                         event->notify_rx.attr_handle, event->notify_rx.om,
                         operationId);
        }
        return 0;
      default:
        return 0;
    }
  }

  Candidate *candidateFor(const ble_addr_t &address) {
    Candidate *oldest = &candidates_[0];
    for (size_t index = 0; index < kCandidateCount; ++index) {
      Candidate &candidate = candidates_[index];
      if (candidate.used && candidate.address.type == address.type &&
          addressEqual(candidate.address.val, address.val)) {
        return &candidate;
      }
      if (!candidate.used) {
        oldest = &candidate;
        break;
      }
      if (candidate.sequence < oldest->sequence) {
        oldest = &candidate;
      }
    }
    *oldest = {};
    oldest->used = true;
    oldest->address = address;
    return oldest;
  }

  void onAdvertisement(const ble_gap_disc_desc &discovery,
                       uint32_t operationId) {
    portENTER_CRITICAL(&mux_);
    const bool scanning =
        (state_ == State::Scanning || state_ == State::Backoff) &&
        operationId == scanOperationId_;
    portEXIT_CRITICAL(&mux_);
    if (!scanning) {
      return;
    }
    portENTER_CRITICAL(&mux_);
    ++advertisementsSeen_;
    portEXIT_CRITICAL(&mux_);
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, discovery.data,
                                discovery.length_data) != 0) {
      portENTER_CRITICAL(&mux_);
      ++malformedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
      return;
    }
    portENTER_CRITICAL(&advertMux_);
    Candidate *candidate = candidateFor(discovery.addr);
    candidate->sequence = ++candidateSequence_;
    if (discovery.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
        discovery.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
      candidate->connectable = true;
    }
    nimbleAccumulateAdvertisementName(
        fields.name, fields.name_len, fields.name_is_complete != 0,
        candidate->advertisement);
    for (uint8_t index = 0; index < fields.num_uuids16; ++index) {
      nimbleAccumulateAdvertisementUuid16(
          ble_uuid_u16(&fields.uuids16[index].u), candidate->advertisement);
    }

    const bool compatible =
        nimbleAdvertisementIsCompatible(candidate->advertisement);
    const bool addressMatches =
        filterPresent_ && addressEqual(filterAddress_, discovery.addr.val);
    const uint32_t receivedAtMs = nowMs();
    const NimblePeerKey peer = peerKey(discovery.addr);
    portENTER_CRITICAL(&mux_);
    const bool wakesBackoff =
        state_ == State::Backoff && backoffPeerPresent_ &&
        nimblePeerKeyEqual(peer, backoffPeer_);
    portEXIT_CRITICAL(&mux_);
    const bool negativeCached =
        (compatible || addressMatches) && negativeCache_.contains(peer, receivedAtMs);
    if (compatible || addressMatches) {
      portENTER_CRITICAL(&mux_);
      ++compatibleAdvertisements_;
      if (!timing_.has(ScaleBleTimingFirstCompatibleAdvertisement)) {
        timing_.firstCompatibleAdvertisementMs = receivedAtMs;
        timing_.recordedFlags |=
            ScaleBleTimingFirstCompatibleAdvertisement;
      }
      if (compatible) {
        formatAddress(candidate->address.val, seenAddress_,
                      sizeof(seenAddress_));
        strncpy(seenName_, candidate->advertisement.name,
                sizeof(seenName_) - 1);
        seenName_[sizeof(seenName_) - 1] = '\0';
        seenPending_ = true;
      }
      portEXIT_CRITICAL(&mux_);
    }
    if (negativeCached) {
      portENTER_CRITICAL(&mux_);
      ++negativeCacheHits_;
      ++discardedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
      portEXIT_CRITICAL(&advertMux_);
      return;
    }
    if (candidate->connectable &&
        ((!filterPresent_ && compatible) || addressMatches)) {
      Event selected = {};
      selected.type = EventType::Candidate;
      selected.address = candidate->address;
      memcpy(selected.name, candidate->advertisement.name,
             sizeof(selected.name));
      bool shouldQueue = false;
      portENTER_CRITICAL(&mux_);
      if ((state_ == State::Scanning || wakesBackoff) &&
          operationId == scanOperationId_ && !candidateQueued_) {
        candidateQueued_ = true;
        selected.generation = generation_;
        selected.operationId = operationId;
        candidateMailbox_ = selected;
        candidatePending_ = true;
        shouldQueue = true;
      }
      portEXIT_CRITICAL(&mux_);
      (void)shouldQueue;
    } else if (!compatible && !addressMatches) {
      portENTER_CRITICAL(&mux_);
      ++discardedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
    }
    portEXIT_CRITICAL(&advertMux_);
  }

  void onNotification(uint16_t connectionHandle, uint16_t attributeHandle,
                      os_mbuf *buffer, uint32_t operationId) {
    if (connectionHandle != connectionHandle_ ||
        attributeHandle != readHandle_ || buffer == nullptr) {
      return;
    }
    const uint16_t length = OS_MBUF_PKTLEN(buffer);
    if (length == 0 || length > MAX_BLE_PACKET_LENGTH) {
      portENTER_CRITICAL(&mux_);
      ++rejectedPackets_;
      if (invalidNotificationStreak_ != 0xff) {
        ++invalidNotificationStreak_;
      }
      if (invalidNotificationStreak_ >= MAX_CONSECUTIVE_REJECTED_PACKETS) {
        invalidNotificationStream_ = true;
      }
      portEXIT_CRITICAL(&mux_);
      return;
    }
    RxFrame frame = {};
    frame.operationId = operationId;
    frame.receivedAtMs = nowMs();
    frame.length = static_cast<uint8_t>(length);
    if (os_mbuf_copydata(buffer, 0, length, frame.data) != 0) {
      portENTER_CRITICAL(&mux_);
      ++mbufFailures_;
      mbufFailed_ = true;
      portEXIT_CRITICAL(&mux_);
      return;
    }
    portENTER_CRITICAL(&mux_);
    if (operationId != linkOperationId_ ||
        connectionHandle != connectionHandle_ ||
        attributeHandle != readHandle_) {
      ++staleCallbacks_;
    } else if (rxCount_ == kRxFrameCount) {
      ++rxDrops_;
      rxOverflowed_ = true;
    } else {
      frame.generation = generation_;
      rxFrames_[rxTail_] = frame;
      rxTail_ = (rxTail_ + 1) % kRxFrameCount;
      ++rxCount_;
      if (rxCount_ > rxHighWater_) {
        rxHighWater_ = rxCount_;
      }
    }
    portEXIT_CRITICAL(&mux_);
  }

  int onService(uint16_t connectionHandle, const ble_gatt_error *error,
                const ble_gatt_svc *service, uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && service != nullptr) {
      portENTER_CRITICAL(&mux_);
      const bool current = operationId == gattOperationId_ &&
                           connectionHandle == connectionHandle_;
      if (!current) {
        ++staleCallbacks_;
      } else if (serviceCount_ < kServiceCount) {
        services_[serviceCount_++] = {service->start_handle,
                                      service->end_handle};
      } else {
        serviceOverflowed_ = true;
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    }
    pushControlEvent(EventType::ServicesComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onCharacteristic(uint16_t connectionHandle,
                       const ble_gatt_error *error,
                       const ble_gatt_chr *characteristic,
                       uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && characteristic != nullptr) {
      for (size_t index = 0; index < scaleProtocolCount(); ++index) {
        const ScaleProtocol *protocol = scaleProtocolAt(index);
        ble_uuid_any_t uuid = {};
        const bool readMatch =
            protocol != nullptr &&
            ble_uuid_from_str(&uuid, protocol->readUuid) == 0 &&
            ble_uuid_cmp(&uuid.u, &characteristic->uuid.u) == 0;
        const bool writeMatch =
            protocol != nullptr &&
            ble_uuid_from_str(&uuid, protocol->writeUuid) == 0 &&
            ble_uuid_cmp(&uuid.u, &characteristic->uuid.u) == 0;
        portENTER_CRITICAL(&mux_);
        const bool current = operationId == gattOperationId_ &&
                             connectionHandle == connectionHandle_ &&
                             serviceIndex_ < serviceCount_;
        if (current) {
          ProtocolHandles &handles = protocolHandles_[index];
          if (handles.read != 0 && handles.readService == serviceIndex_ &&
              characteristic->def_handle > handles.read &&
              characteristic->def_handle - 1 < handles.readEnd) {
            handles.readEnd = characteristic->def_handle - 1;
          }
          if (readMatch && handles.read == 0) {
            handles.read = characteristic->val_handle;
            handles.readEnd = services_[serviceIndex_].end;
            handles.readProperties = characteristic->properties;
            handles.readService = static_cast<uint8_t>(serviceIndex_);
          }
          if (writeMatch && handles.write == 0) {
            handles.write = characteristic->val_handle;
            handles.writeProperties = characteristic->properties;
          }
        } else {
          ++staleCallbacks_;
        }
        portEXIT_CRITICAL(&mux_);
        if (!current) {
          return 0;
        }
      }
      return 0;
    }
    pushControlEvent(EventType::CharacteristicsComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onDescriptor(uint16_t connectionHandle, const ble_gatt_error *error,
                   uint16_t, const ble_gatt_dsc *descriptor,
                   uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && descriptor != nullptr) {
      portENTER_CRITICAL(&mux_);
      const bool current = operationId == gattOperationId_ &&
                           connectionHandle == connectionHandle_;
      if (!current) {
        ++staleCallbacks_;
      } else if (ble_uuid_u16(&descriptor->uuid.u) ==
                 BLE_GATT_DSC_CLT_CFG_UUID16) {
        cccdHandle_ = descriptor->handle;
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    }
    pushControlEvent(EventType::DescriptorsComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onWrite(uint16_t connectionHandle, const ble_gatt_error *error,
              uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    TaskHandle_t waiter = nullptr;
    WritePurpose purpose = WritePurpose::None;
    portENTER_CRITICAL(&mux_);
    const bool current = operationId == gattOperationId_ &&
                         connectionHandle == connectionHandle_;
    if (current) {
      purpose = writePurpose_;
      writeResult_ = error->status;
      writeCompleted_ = true;
      if (error->status == BLE_HS_ENOMEM) {
        ++mbufFailures_;
      }
      waiter = writeWaiter_;
      if (purpose != WritePurpose::Command) {
        writePurpose_ = WritePurpose::None;
      }
    } else {
      ++staleCallbacks_;
    }
    portEXIT_CRITICAL(&mux_);
    if (!current) {
      return 0;
    }
    if (purpose == WritePurpose::Command && waiter != nullptr) {
      xTaskNotifyGive(waiter);
    } else if (purpose != WritePurpose::None) {
      pushControlEvent(EventType::WriteComplete, error->status,
                       connectionHandle, operationId);
    }
    return 0;
  }

  bool pushControlEvent(EventType type, int32_t status,
                        uint16_t connectionHandle, uint32_t operationId) {
    Event event = {};
    event.type = type;
    event.operationId = operationId;
    event.status = status;
    event.connectionHandle = connectionHandle;
    bool pushed = false;
    portENTER_CRITICAL(&mux_);
    event.generation = generation_;
    pushed = controlEvents_.push(event);
    if (!pushed) {
      eventOverflowed_ = true;
    }
    portEXIT_CRITICAL(&mux_);
    return pushed;
  }

  bool pushCriticalEvent(EventType type, int32_t status,
                         uint16_t connectionHandle, uint32_t operationId) {
    Event event = {};
    event.type = type;
    event.operationId = operationId;
    event.status = status;
    event.connectionHandle = connectionHandle;
    portENTER_CRITICAL(&mux_);
    event.generation = generation_;
    const bool pushed = criticalEvents_.push(event);
    if (!pushed) {
      criticalOverflowed_ = true;
    }
    TaskHandle_t waiter = nullptr;
    if (type == EventType::Disconnected) {
      waiter = writeWaiter_;
      writeResult_ = status;
      writeInterrupted_ = true;
    }
    portEXIT_CRITICAL(&mux_);
    if (waiter != nullptr) {
      xTaskNotifyGive(waiter);
    }
    return pushed;
  }

  bool popCriticalEvent(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = criticalEvents_.pop(event);
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popControlEvent(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = controlEvents_.pop(event);
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popCandidate(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = candidatePending_;
    if (present) {
      event = candidateMailbox_;
      candidatePending_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popRx(RxFrame &frame) {
    portENTER_CRITICAL(&mux_);
    const bool present = rxCount_ != 0;
    if (present) {
      frame = rxFrames_[rxHead_];
      rxHead_ = (rxHead_ + 1) % kRxFrameCount;
      --rxCount_;
    }
    portEXIT_CRITICAL(&mux_);
    return present && frame.generation == generation_ &&
           frame.operationId == linkOperationId_;
  }

  static NimblePeerKey peerKey(const ble_addr_t &address) {
    NimblePeerKey key = {};
    memcpy(key.address, address.val, sizeof(key.address));
    key.type = address.type;
    return key;
  }

  uint32_t nextOperationIdLocked() {
    ++operationId_;
    if (operationId_ == 0) {
      operationId_ = 1;
    }
    return operationId_;
  }

  uint32_t beginOperation(CallbackDomain domain) {
    portENTER_CRITICAL(&mux_);
    const uint32_t id = nextOperationIdLocked();
    if (domain == CallbackDomain::Scan) {
      scanOperationId_ = id;
    } else if (domain == CallbackDomain::Link) {
      linkOperationId_ = id;
    } else {
      gattOperationId_ = id;
    }
    portEXIT_CRITICAL(&mux_);
    return id;
  }

  uint32_t beginGeneration() {
    portENTER_CRITICAL(&mux_);
    ++generation_;
    if (generation_ == 0) {
      generation_ = 1;
    }
    const uint32_t generation = generation_;
    portEXIT_CRITICAL(&mux_);
    return generation;
  }

  void invalidateGeneration() {
    portENTER_CRITICAL(&mux_);
    ++generation_;
    if (generation_ == 0) {
      generation_ = 1;
    }
    scanOperationId_ = 0;
    linkOperationId_ = 0;
    gattOperationId_ = 0;
    portEXIT_CRITICAL(&mux_);
  }

  void enterState(State state, uint32_t timeoutMs = 0) {
    portENTER_CRITICAL(&mux_);
    state_ = state;
    stateEnteredAtMs_ = nowMs();
    stateTimeoutMs_ = timeoutMs;
    stateDeadlineArmed_ = timeoutMs != 0;
    portEXIT_CRITICAL(&mux_);
  }

  bool beginConfiguredScan(bool restart, State initialState = State::Scanning,
                           uint32_t stateTimeoutMs = 0) {
    beginGeneration();
    lifecycleActive_ = true;
    syncGeneration_ = shotStopperBleRuntimeSyncGeneration();
    clearScanData();
    timing_ = {};
    const uint32_t scanOperationId = beginOperation(CallbackDomain::Scan);

    ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = scanInterval_;
    params.window = scanWindow_;
    params.filter_policy = 0;
    params.limited = 0;
    const int rc = ble_gap_disc(shotStopperBleRuntimeOwnAddressType(),
                                BLE_HS_FOREVER, &params, gapCallback,
                                callbackArg(scanOperationId));
    if (rc != 0) {
      backoff_.reset();
      portENTER_CRITICAL(&mux_);
      backoffPeerPresent_ = false;
      portEXIT_CRITICAL(&mux_);
      finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED, rc);
      return false;
    }
    enterState(initialState, stateTimeoutMs);
    scanStartedAt_ = stateEnteredAtMs_;
    timing_.scanStartedMs = scanStartedAt_;
    timing_.recordedFlags = ScaleBleTimingScanStarted;
    ++scanStarts_;
    if (restart) {
      ++scanRestarts_;
    }
    if (debug_) {
      scaleLogDebug("active scan started (%u/%u)", scanInterval_, scanWindow_);
    }
    return true;
  }

  void service() {
    const uint32_t runtimeGeneration =
        shotStopperBleRuntimeSyncGeneration();
    if (syncGeneration_ != 0 && syncGeneration_ != runtimeGeneration) {
      const ShotStopperBleHealth health = shotStopperBleRuntimeHealth();
      finishLink(false, ScaleDisconnectReason::HOST_RESET,
                 health.lastResetReason);
      syncGeneration_ = runtimeGeneration;
    }
    if (!shotStopperBleRuntimeReady() && state_ != State::Idle) {
      finishLink(false, ScaleDisconnectReason::HOST_RESET, BLE_HS_ENOTSYNCED);
      return;
    }
    bool criticalOverflowed = false;
    bool controlOverflowed = false;
    bool rxOverflowed = false;
    bool mbufFailed = false;
    bool invalidNotificationStream = false;
    portENTER_CRITICAL(&mux_);
    criticalOverflowed = criticalOverflowed_;
    controlOverflowed = eventOverflowed_;
    rxOverflowed = rxOverflowed_;
    mbufFailed = mbufFailed_;
    invalidNotificationStream = invalidNotificationStream_;
    criticalOverflowed_ = false;
    eventOverflowed_ = false;
    rxOverflowed_ = false;
    mbufFailed_ = false;
    invalidNotificationStream_ = false;
    portEXIT_CRITICAL(&mux_);
    if (criticalOverflowed || controlOverflowed) {
      finishLink(true, ScaleDisconnectReason::EVENT_QUEUE_OVERFLOW,
                 BLE_HS_ENOMEM);
      return;
    }
    if (rxOverflowed) {
      finishLink(true, ScaleDisconnectReason::RX_QUEUE_OVERFLOW,
                 BLE_HS_ENOMEM);
      return;
    }
    if (mbufFailed) {
      finishLink(true, ScaleDisconnectReason::MBUF_ALLOCATION_FAILED,
                 BLE_HS_ENOMEM);
      return;
    }
    if (invalidNotificationStream) {
      finishLink(true, ScaleDisconnectReason::INVALID_PACKET_STREAM,
                 BLE_HS_EBADDATA);
      return;
    }

    Event event = {};
    while (popCriticalEvent(event)) {
      if (!eventMatches(event)) {
        noteStaleCallback();
        continue;
      }
      handleEvent(event);
    }
    if (popCandidate(event)) {
      if (eventMatches(event)) {
        handleEvent(event);
      } else {
        noteStaleCallback();
      }
    }
    while (popControlEvent(event)) {
      if (!eventMatches(event)) {
        noteStaleCallback();
        continue;
      }
      handleEvent(event);
    }

    if (isConnecting() && connectStartedAt_ != 0 &&
        elapsedMs(connectStartedAt_) >= SCALE_CONNECT_BUDGET_MS) {
      finishLink(true, ScaleDisconnectReason::OPERATION_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return;
    }
    if (state_ == State::Backoff && !backoff_.active(nowMs())) {
      backoff_.reset();
      backoffPeerPresent_ = false;
      // GAP scanning remains active during backoff; only candidate selection
      // was gated. No radio restart is needed when the timer expires.
      enterState(State::Scanning);
      return;
    }
    if (stateDeadlineArmed_ &&
        elapsedMs(stateEnteredAtMs_) >= stateTimeoutMs_) {
      finishLink(true, ScaleDisconnectReason::OPERATION_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return;
    }
  }

  bool eventMatches(const Event &event) const {
    if (event.generation != generation_ || event.operationId == 0) {
      return false;
    }
    switch (event.type) {
      case EventType::Candidate:
      case EventType::ScanComplete:
        return event.operationId == scanOperationId_;
      case EventType::ConnectComplete:
      case EventType::Disconnected:
        return event.operationId == linkOperationId_;
      case EventType::ServicesComplete:
      case EventType::CharacteristicsComplete:
      case EventType::DescriptorsComplete:
      case EventType::WriteComplete:
        return event.operationId == gattOperationId_;
    }
    return false;
  }

  void handleEvent(const Event &event) {
    switch (event.type) {
      case EventType::Candidate: {
        const NimblePeerKey candidatePeer = peerKey(event.address);
        const bool wakesBackoff =
            state_ == State::Backoff && backoffPeerPresent_ &&
            nimblePeerKeyEqual(candidatePeer, backoffPeer_);
        if (state_ != State::Scanning && !wakesBackoff) {
          noteStaleCallback();
          return;
        }
        if (wakesBackoff) {
          // The host callback only queued evidence. The worker owns the state
          // transition and can safely cancel scan before reconnecting.
          backoff_.reset();
          backoffPeerPresent_ = false;
          enterState(State::Scanning);
        }
        selectedAddress_ = event.address;
        strncpy(name_, event.name, sizeof(name_) - 1);
        name_[sizeof(name_) - 1] = '\0';
        formatAddress(event.address.val, address_, sizeof(address_));
        identityPresent_ = true;
        backoff_.reset();
        enterState(State::CancelPending, kScanCancelTimeoutMs);
        connectStartedAt_ = nowMs();
        ++scanCancels_;
        const int cancelResult = ble_gap_disc_cancel();
        // ble_gap_disc_cancel() is synchronous: a zero return means scanning
        // has been fully aborted and a connect procedure can start
        // immediately.  A manual cancellation does not produce a later
        // BLE_GAP_EVENT_DISC_COMPLETE callback.
        if (cancelResult == 0 || cancelResult == BLE_HS_EALREADY) {
          beginConnect();
        } else {
          finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED,
                     cancelResult);
        }
        return;
      }

      case EventType::ScanComplete:
        if (state_ == State::CancelPending) {
          beginConnect();
        } else if (state_ == State::Scanning || state_ == State::Backoff) {
          finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED,
                     event.status);
        } else {
          noteStaleCallback();
        }
        return;

      case EventType::ConnectComplete:
        if (state_ != State::Connecting) {
          noteStaleCallback();
          return;
        }
        if (event.status != 0) {
          ++connectionFailures_;
          finishLink(false, mapRawDisconnectReason(event.status),
                     event.status);
          return;
        }
        connectionHandle_ = event.connectionHandle;
        beginServiceDiscovery();
        return;

      case EventType::Disconnected:
        if (event.connectionHandle != connectionHandle_) {
          noteStaleCallback();
          return;
        }
        finishLink(false, mapRawDisconnectReason(event.status), event.status);
        return;

      case EventType::ServicesComplete:
        if (state_ != State::DiscoveringServices) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE || serviceOverflowed_ ||
            serviceCount_ == 0) {
          ++discoveryFailures_;
          cooldownSelected(kGattFailureCooldownMs);
          finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED,
                     serviceOverflowed_ ? BLE_HS_ENOMEM : event.status);
          return;
        }
        serviceIndex_ = 0;
        beginCharacteristicDiscovery();
        return;

      case EventType::CharacteristicsComplete:
        if (state_ != State::DiscoveringCharacteristics) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE) {
          ++discoveryFailures_;
          cooldownSelected(kGattFailureCooldownMs);
          finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED,
                     event.status);
          return;
        }
        ++serviceIndex_;
        if (serviceIndex_ < serviceCount_) {
          beginCharacteristicDiscovery();
        } else {
          selectProtocolAndDiscoverCccd();
        }
        return;

      case EventType::DescriptorsComplete:
        if (state_ != State::DiscoveringDescriptors) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE || cccdHandle_ == 0) {
          ++subscriptionFailures_;
          cooldownSelected(kGattFailureCooldownMs);
          finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED,
                     cccdHandle_ == 0 ? BLE_HS_ENOENT : event.status);
          return;
        }
        beginSubscription();
        return;

      case EventType::WriteComplete:
        if (state_ == State::Subscribing) {
          if (event.status != 0) {
            ++subscriptionFailures_;
            cooldownSelected(kGattFailureCooldownMs);
            finishLink(true,
                       event.status == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::SUBSCRIBE_FAILED,
                       event.status);
            return;
          }
          initWriteIndex_ = 0;
          enterState(State::Initializing, BLE_OPERATION_TIMEOUT_MS);
          beginNextInitWrite();
        } else if (state_ == State::Initializing) {
          if (event.status != 0) {
            ++writeFailures_;
            cooldownSelected(kGattFailureCooldownMs);
            finishLink(true,
                       event.status == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                       event.status);
            return;
          }
          ++initWriteIndex_;
          beginNextInitWrite();
        }
        return;
    }
  }

  void beginConnect() {
    enterState(State::Connecting, BLE_CONNECT_TIMEOUT_MS);
    if (!timing_.has(ScaleBleTimingConnectIssued)) {
      timing_.connectIssuedMs = nowMs();
      timing_.recordedFlags |= ScaleBleTimingConnectIssued;
    }
    const uint32_t linkOperationId = beginOperation(CallbackDomain::Link);
    if (connectAttempts_ != 0xff) {
      ++connectAttempts_;
    }
    ++connectAttemptsTotal_;
    const int rc = ble_gap_connect(shotStopperBleRuntimeOwnAddressType(),
                                   &selectedAddress_, BLE_CONNECT_TIMEOUT_MS,
                                   nullptr, gapCallback,
                                   callbackArg(linkOperationId));
    if (rc != 0) {
      ++connectionFailures_;
      finishLink(false, ScaleDisconnectReason::CONNECT_FAILED, rc);
    }
  }

  void beginServiceDiscovery() {
    serviceCount_ = 0;
    serviceOverflowed_ = false;
    memset(services_, 0, sizeof(services_));
    memset(protocolHandles_, 0, sizeof(protocolHandles_));
    enterState(State::DiscoveringServices, BLE_DISCOVER_TIMEOUT_MS);
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_svcs(connectionHandle_, serviceCallback,
                                           callbackArg(gattOperationId));
    if (rc != 0) {
      ++discoveryFailures_;
      cooldownSelected(kGattFailureCooldownMs);
      finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED, rc);
    }
  }

  void beginCharacteristicDiscovery() {
    enterState(State::DiscoveringCharacteristics, BLE_DISCOVER_TIMEOUT_MS);
    const ServiceRange &service = services_[serviceIndex_];
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_chrs(
        connectionHandle_, service.start, service.end, characteristicCallback,
        callbackArg(gattOperationId));
    if (rc != 0) {
      ++discoveryFailures_;
      cooldownSelected(kGattFailureCooldownMs);
      finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED, rc);
    }
  }

  void selectProtocolAndDiscoverCccd() {
    protocol_ = nullptr;
    for (size_t index = 0; index < scaleProtocolCount(); ++index) {
      const ScaleProtocol *candidate = scaleProtocolAt(index);
      const ProtocolHandles &handles = protocolHandles_[index];
      const bool canSubscribe =
          (handles.readProperties &
           (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE)) != 0;
      const bool canWrite =
          (handles.writeProperties &
           (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) != 0;
      const bool nameAllowed =
          candidate != nullptr &&
          (!candidate->requireAdvertisedName ||
           scaleNameMatchesProtocol(name_, candidate));
      if (candidate != nullptr && handles.read != 0 && handles.write != 0 &&
          canSubscribe && canWrite && nameAllowed) {
        protocol_ = candidate;
        readHandle_ = handles.read;
        readEndHandle_ = handles.readEnd;
        readProperties_ = handles.readProperties;
        writeHandle_ = handles.write;
        writeProperties_ = handles.writeProperties;
        break;
      }
    }
    if (protocol_ == nullptr) {
      cooldownSelected(kUnsupportedCooldownMs);
      finishLink(true, ScaleDisconnectReason::UNSUPPORTED_SCALE,
                 BLE_HS_ENOENT);
      return;
    }
    cccdHandle_ = 0;
    if (readEndHandle_ <= readHandle_) {
      ++subscriptionFailures_;
      cooldownSelected(kGattFailureCooldownMs);
      finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED,
                 BLE_HS_EINVAL);
      return;
    }
    enterState(State::DiscoveringDescriptors, BLE_DISCOVER_TIMEOUT_MS);
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_dscs(
        connectionHandle_, readHandle_, readEndHandle_, descriptorCallback,
        callbackArg(gattOperationId));
    if (rc != 0) {
      ++subscriptionFailures_;
      cooldownSelected(kGattFailureCooldownMs);
      finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED, rc);
    }
  }

  void beginSubscription() {
    const uint16_t value =
        (readProperties_ & BLE_GATT_CHR_PROP_NOTIFY) != 0 ? 1 : 2;
    const uint8_t cccd[2] = {static_cast<uint8_t>(value & 0xff),
                             static_cast<uint8_t>(value >> 8)};
    enterState(State::Subscribing, BLE_OPERATION_TIMEOUT_MS);
    if (!submitWrite(cccdHandle_, cccd, sizeof(cccd),
                     WritePurpose::Subscribe, true)) {
      ++subscriptionFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::SUBSCRIBE_FAILED,
                 lastRawStatus_);
    }
  }

  void beginNextInitWrite() {
    if (protocol_ == nullptr || initWriteIndex_ >= protocol_->initWriteCount) {
      finishReady();
      return;
    }
    const ScalePayload &payload = protocol_->initWrites[initWriteIndex_];
    if (payload.data == nullptr || payload.length <= 0 ||
        payload.length > SCALE_MAX_COMMAND_LENGTH) {
      ++writeFailures_;
      finishLink(true, ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                 BLE_HS_EINVAL);
      return;
    }
    const bool withResponse =
        (writeProperties_ & BLE_GATT_CHR_PROP_WRITE) != 0;
    // Each initialization transaction owns a fresh bounded deadline. This is
    // important for protocols with several writes: one slow/lost callback must
    // not inherit an already-expired deadline from the previous transaction.
    enterState(State::Initializing, BLE_OPERATION_TIMEOUT_MS);
    if (!submitWrite(writeHandle_, payload.data,
                     static_cast<uint16_t>(payload.length),
                     WritePurpose::Initialize, withResponse)) {
      ++writeFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                 lastRawStatus_);
      return;
    }
    if (!withResponse) {
      ++initWriteIndex_;
      beginNextInitWrite();
    }
  }

  bool submitWrite(uint16_t handle, const uint8_t *data, uint16_t length,
                   WritePurpose purpose, bool withResponse) {
    if (connectionHandle_ == kInvalidHandle || handle == 0 || data == nullptr ||
        length == 0 || length > SCALE_MAX_COMMAND_LENGTH) {
      return false;
    }
    if (!withResponse) {
      const int rc = ble_gattc_write_no_rsp_flat(connectionHandle_, handle,
                                                 data, length);
      lastRawStatus_ = rc;
      if (rc == BLE_HS_ENOMEM) {
        ++mbufFailures_;
      }
      return rc == 0;
    }
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    portENTER_CRITICAL(&mux_);
    writePurpose_ = purpose;
    writeCompleted_ = false;
    writeInterrupted_ = false;
    writeResult_ = BLE_HS_EUNKNOWN;
    writeWaiter_ = purpose == WritePurpose::Command
                       ? xTaskGetCurrentTaskHandle()
                       : nullptr;
    portEXIT_CRITICAL(&mux_);
    const int rc = ble_gattc_write_flat(connectionHandle_, handle, data,
                                        length, writeCallback,
                                        callbackArg(gattOperationId));
    if (rc != 0) {
      portENTER_CRITICAL(&mux_);
      writePurpose_ = WritePurpose::None;
      writeWaiter_ = nullptr;
      portEXIT_CRITICAL(&mux_);
      lastRawStatus_ = rc;
      if (rc == BLE_HS_ENOMEM) {
        ++mbufFailures_;
      }
      return false;
    }
    return true;
  }

  ScaleCommandResult writeCommand(const uint8_t *data, uint16_t length) {
    const bool withResponse =
        (writeProperties_ & BLE_GATT_CHR_PROP_WRITE) != 0;
    if (!withResponse) {
      if (submitWrite(writeHandle_, data, length, WritePurpose::Command,
                      false)) {
        return ScaleCommandResult::Ok;
      }
      ++writeFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::COMMAND_WRITE_FAILED,
                 lastRawStatus_);
      return ScaleCommandResult::WriteFailed;
    }
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (!submitWrite(writeHandle_, data, length, WritePurpose::Command, true)) {
      ++writeFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::COMMAND_WRITE_FAILED,
                 lastRawStatus_);
      return ScaleCommandResult::WriteFailed;
    }
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BLE_OPERATION_TIMEOUT_MS));
    portENTER_CRITICAL(&mux_);
    const bool completed = writeCompleted_;
    const bool interrupted = writeInterrupted_;
    const int result = writeResult_;
    writePurpose_ = WritePurpose::None;
    writeWaiter_ = nullptr;
    portEXIT_CRITICAL(&mux_);
    if (completed && result == 0) {
      return ScaleCommandResult::Ok;
    }
    lastRawStatus_ = (completed || interrupted) ? result : BLE_HS_ETIMEOUT;
    ++writeFailures_;
    finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                         ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                         : ScaleDisconnectReason::COMMAND_WRITE_FAILED,
               lastRawStatus_);
    return ScaleCommandResult::WriteFailed;
  }

  void finishReady() {
    enterState(State::Ready);
    connectedAt_ = nowMs();
    timing_.readyMs = connectedAt_;
    timing_.recordedFlags |= ScaleBleTimingReady;
    const uint16_t heartbeatPeriod =
        protocol_ != nullptr && protocol_->features.heartbeatPeriodMs != 0
            ? protocol_->features.heartbeatPeriodMs
            : HEARTBEAT_PERIOD_MS;
    lastHeartbeat_ = connectedAt_ - heartbeatPeriod;
    hasValidPacket_ = false;
    invalidNotificationStreak_ = 0;
    hasTimer_ = false;
    currentTimerMs_ = 0;
    lastTimerPacket_ = 0;
    if (successfulConnections_ != 0) {
      ++reconnects_;
    }
    ++successfulConnections_;
    backoff_.reset();
    portENTER_CRITICAL(&advertMux_);
    negativeCache_.erase(peerKey(selectedAddress_));
    portEXIT_CRITICAL(&advertMux_);
    if (timing_.has(ScaleBleTimingFirstCompatibleAdvertisement)) {
      lastAdvertisementToConnectMs_ =
          timing_.connectIssuedMs - timing_.firstCompatibleAdvertisementMs;
      lastAdvertisementToReadyMs_ =
          timing_.readyMs - timing_.firstCompatibleAdvertisementMs;
    }
    if (debug_) {
      scaleLogDebug("ready: %s @ %s", protocol_->id, address_);
    }
  }

  void cooldownSelected(uint32_t cooldownMs) {
    if (!identityPresent_) {
      return;
    }
    portENTER_CRITICAL(&advertMux_);
    negativeCache_.insert(peerKey(selectedAddress_), nowMs(), cooldownMs);
    portEXIT_CRITICAL(&advertMux_);
    ++negativeCacheInsertions_;
  }

  bool finishLink(bool terminatePeer, ScaleDisconnectReason reason,
                  int32_t rawStatus) {
    if (!lifecycleActive_) {
      if (state_ == State::Backoff &&
          reason == ScaleDisconnectReason::USER_REQUEST) {
        backoff_.reset();
        enterState(State::Idle);
      } else if (state_ != State::Idle && state_ != State::Backoff) {
        enterState(State::Idle);
      }
      if (reason != ScaleDisconnectReason::NONE) {
        lastReason_ = reason;
        lastRawStatus_ = rawStatus;
      }
      ++duplicateCleanups_;
      return false;
    }
    const State previous = state_;
    const uint16_t oldHandle = connectionHandle_;
    const uint32_t finishedGeneration = generation_;
    lifecycleActive_ = false;
    invalidateGeneration();
    enterState(State::Idle);
    ++cleanupCount_;
    if (reason != ScaleDisconnectReason::NONE) {
      lastReason_ = reason;
      lastRawStatus_ = rawStatus;
    }
    if (previous == State::Scanning || previous == State::CancelPending ||
        previous == State::Backoff) {
      (void)ble_gap_disc_cancel();
    } else if (previous == State::Connecting) {
      (void)ble_gap_conn_cancel();
    }
    connectionHandle_ = kInvalidHandle;
    if (terminatePeer && oldHandle != kInvalidHandle) {
      (void)ble_gap_terminate(oldHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
    protocol_ = nullptr;
    readHandle_ = 0;
    writeHandle_ = 0;
    cccdHandle_ = 0;
    readEndHandle_ = 0;
    readProperties_ = 0;
    writeProperties_ = 0;
    scanStartedAt_ = 0;
    connectStartedAt_ = 0;
    connectedAt_ = 0;
    lastPacket_ = 0;
    packetPeriod_ = 0;
    lastHeartbeat_ = 0;
    hasValidPacket_ = false;
    consecutiveRejectedPackets_ = 0;
    hasTimer_ = false;
    currentTimerMs_ = 0;
    lastTimerPacket_ = 0;
    invalidNotificationStreak_ = 0;
    TaskHandle_t waiterToWake = nullptr;
    portENTER_CRITICAL(&mux_);
    criticalEvents_.clear();
    controlEvents_.clear();
    candidatePending_ = false;
    rxHead_ = rxTail_ = rxCount_ = 0;
    writePurpose_ = WritePurpose::None;
    waiterToWake = writeWaiter_;
    if (waiterToWake != nullptr) {
      writeResult_ = rawStatus;
      writeInterrupted_ = true;
    }
    writeWaiter_ = nullptr;
    eventOverflowed_ = false;
    criticalOverflowed_ = false;
    rxOverflowed_ = false;
    mbufFailed_ = false;
    invalidNotificationStream_ = false;
    portEXIT_CRITICAL(&mux_);
    if (waiterToWake != nullptr && waiterToWake != xTaskGetCurrentTaskHandle()) {
      xTaskNotifyGive(waiterToWake);
    }
    if (reason == ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH) {
      const NimblePeerKey failedPeer = peerKey(selectedAddress_);
      const bool failedPeerPresent = identityPresent_;
      const uint32_t entropy = static_cast<uint32_t>(selectedAddress_.val[0]) |
                               generation_ << 8;
      const uint32_t delayMs = backoff_.schedule(nowMs(), entropy);
      ++backoffCount_;
      portENTER_CRITICAL(&mux_);
      backoffPeer_ = failedPeer;
      backoffPeerPresent_ = failedPeerPresent;
      portEXIT_CRITICAL(&mux_);
      // Continue active scanning while candidate selection is delayed. A new
      // connectable advertisement from this exact peer wakes the retry early.
      (void)beginConfiguredScan(false, State::Backoff, delayMs);
    } else {
      backoff_.reset();
      portENTER_CRITICAL(&mux_);
      backoffPeerPresent_ = false;
      portEXIT_CRITICAL(&mux_);
    }
    if (debug_ && reason != ScaleDisconnectReason::NONE) {
      scaleLogDebug("link finished: %s raw=%ld gen=%lu",
                    disconnectReasonName(reason), static_cast<long>(rawStatus),
                    static_cast<unsigned long>(finishedGeneration));
    }
    return true;
  }

  void clearScanData() {
    portENTER_CRITICAL(&advertMux_);
    memset(candidates_, 0, sizeof(candidates_));
    candidateSequence_ = 0;
    portEXIT_CRITICAL(&advertMux_);
    identityPresent_ = false;
    portENTER_CRITICAL(&mux_);
    seenPending_ = false;
    seenAddress_[0] = '\0';
    seenName_[0] = '\0';
    candidateQueued_ = false;
    candidatePending_ = false;
    portEXIT_CRITICAL(&mux_);
  }

  uint32_t maxPacketPeriodMs() const {
    return protocol_ != nullptr && protocol_->features.maxPacketSilenceMs != 0
               ? protocol_->features.maxPacketSilenceMs
               : MAX_PACKET_PERIOD_MS;
  }

  bool supportedPacketLength(int length) const {
    return protocol_ != nullptr && protocol_->supportedPacketLength != nullptr &&
           protocol_->supportedPacketLength(length);
  }

  void rejectPacket() {
    ++rejectedPackets_;
    ++consecutiveRejectedPackets_;
    if (consecutiveRejectedPackets_ >= MAX_CONSECUTIVE_REJECTED_PACKETS) {
      finishLink(true, ScaleDisconnectReason::INVALID_PACKET_STREAM,
                 BLE_HS_EBADDATA);
    }
  }

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  mutable portMUX_TYPE advertMux_ = portMUX_INITIALIZER_UNLOCKED;
  static NimbleScaleClient *activeClient_;
  State state_ = State::Idle;
  bool debug_ = false;
  bool callbackOwner_ = false;
  bool lifecycleActive_ = false;
  uint32_t generation_ = 0;
  uint32_t operationId_ = 0;
  uint32_t scanOperationId_ = 0;
  uint32_t linkOperationId_ = 0;
  uint32_t gattOperationId_ = 0;
  uint32_t syncGeneration_ = 0;
  uint32_t stateEnteredAtMs_ = 0;
  uint32_t stateTimeoutMs_ = 0;
  bool stateDeadlineArmed_ = false;
  int32_t lastRawStatus_ = 0;
  ScaleDisconnectReason lastReason_ = ScaleDisconnectReason::NONE;

  NimbleFixedRing<Event, kCriticalEventCount> criticalEvents_;
  NimbleFixedRing<Event, kEventCount> controlEvents_;
  Event candidateMailbox_ = {};
  bool candidatePending_ = false;
  bool criticalOverflowed_ = false;
  bool eventOverflowed_ = false;
  uint32_t staleCallbacks_ = 0;

  Candidate candidates_[kCandidateCount] = {};
  NimbleNegativeCache negativeCache_;
  NimbleBackoffPolicy backoff_;
  NimblePeerKey backoffPeer_ = {};
  bool backoffPeerPresent_ = false;
  uint32_t candidateSequence_ = 0;
  uint32_t advertisementsSeen_ = 0;
  uint32_t compatibleAdvertisements_ = 0;
  uint32_t discardedAdvertisements_ = 0;
  uint32_t malformedAdvertisements_ = 0;
  uint32_t negativeCacheHits_ = 0;
  uint32_t negativeCacheInsertions_ = 0;
  uint32_t scanStarts_ = 0;
  uint32_t scanCancels_ = 0;
  uint32_t scanRestarts_ = 0;
  bool candidateQueued_ = false;
  ble_addr_t selectedAddress_ = {};
  uint8_t filterAddress_[6] = {};
  bool filterPresent_ = false;
  bool scanAddressFilter_ = false;
  uint16_t scanInterval_ = 0;
  uint16_t scanWindow_ = 0;
  uint32_t scanStartedAt_ = 0;
  bool seenPending_ = false;
  char seenAddress_[SCALE_MAC_CAPACITY] = {};
  char seenName_[SCALE_NAME_CAPACITY] = {};

  ServiceRange services_[kServiceCount] = {};
  size_t serviceCount_ = 0;
  size_t serviceIndex_ = 0;
  bool serviceOverflowed_ = false;
  ProtocolHandles protocolHandles_[kProtocolCapacity] = {};
  const ScaleProtocol *protocol_ = nullptr;
  uint16_t connectionHandle_ = kInvalidHandle;
  uint16_t readHandle_ = 0;
  uint16_t readEndHandle_ = 0;
  uint16_t writeHandle_ = 0;
  uint16_t cccdHandle_ = 0;
  uint8_t readProperties_ = 0;
  uint8_t writeProperties_ = 0;
  size_t initWriteIndex_ = 0;
  uint8_t connectAttempts_ = 0;
  uint32_t connectAttemptsTotal_ = 0;
  uint32_t connectionFailures_ = 0;
  uint32_t discoveryFailures_ = 0;
  uint32_t subscriptionFailures_ = 0;
  uint32_t writeFailures_ = 0;
  uint32_t connectStartedAt_ = 0;

  WritePurpose writePurpose_ = WritePurpose::None;
  TaskHandle_t writeWaiter_ = nullptr;
  bool writeCompleted_ = false;
  bool writeInterrupted_ = false;
  int writeResult_ = 0;

  RxFrame rxFrames_[kRxFrameCount] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  uint16_t rxCount_ = 0;
  uint16_t rxHighWater_ = 0;
  uint32_t rxDrops_ = 0;
  bool rxOverflowed_ = false;
  bool mbufFailed_ = false;
  bool invalidNotificationStream_ = false;
  uint8_t invalidNotificationStreak_ = 0;
  uint32_t mbufFailures_ = 0;

  bool identityPresent_ = false;
  char address_[SCALE_MAC_CAPACITY] = {};
  char name_[SCALE_NAME_CAPACITY] = {};
  ScaleBleTimingSnapshot timing_ = {};
  uint32_t connectedAt_ = 0;
  uint32_t lastHeartbeat_ = 0;
  uint32_t lastPacket_ = 0;
  uint32_t packetPeriod_ = 0;
  uint32_t lastTimerPacket_ = 0;
  uint32_t currentTimerMs_ = 0;
  float currentWeight_ = 0.0f;
  uint32_t rejectedPackets_ = 0;
  uint8_t consecutiveRejectedPackets_ = 0;
  uint32_t reconnects_ = 0;
  uint32_t successfulConnections_ = 0;
  uint32_t cleanupCount_ = 0;
  uint32_t duplicateCleanups_ = 0;
  uint32_t backoffCount_ = 0;
  uint32_t lastAdvertisementToConnectMs_ = 0;
  uint32_t lastAdvertisementToReadyMs_ = 0;
  bool hasValidPacket_ = false;
  bool hasTimer_ = false;
};

NimbleScaleClient *NimbleScaleClient::activeClient_ = nullptr;

NimbleScaleClient &clientFromStorage(void *storage) {
  return *reinterpret_cast<NimbleScaleClient *>(storage);
}

const NimbleScaleClient &clientFromStorage(const void *storage) {
  return *reinterpret_cast<const NimbleScaleClient *>(storage);
}

}  // namespace

EspressoScaleBLE::EspressoScaleBLE(bool debug) {
  static_assert(sizeof(NimbleScaleClient) <= NIMBLE_CLIENT_STORAGE_SIZE,
                "increase fixed NimBLE client storage");
  static_assert(alignof(NimbleScaleClient) <= 8,
                "NimBLE client storage alignment is insufficient");
  new (_nimbleClientStorage) NimbleScaleClient(debug);
}

EspressoScaleBLE::~EspressoScaleBLE() {
  clientFromStorage(_nimbleClientStorage).~NimbleScaleClient();
}

bool EspressoScaleBLE::init(const char *mac) {
  NimbleScaleClient &client = clientFromStorage(_nimbleClientStorage);
  client.disconnect();
  if (!client.startScan(mac, false, BLE_SCAN_NORMAL_INTERVAL,
                        BLE_SCAN_NORMAL_WINDOW, false)) {
    return false;
  }
  const uint32_t startedAt = nowMs();
  while (elapsedMs(startedAt) < SCALE_SCAN_TIMEOUT_MS ||
         client.isConnecting()) {
    if (client.poll()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  client.disconnect();
  return false;
}

bool EspressoScaleBLE::startScan(const char *mac, bool forceRestart,
                                 uint16_t interval, uint16_t window,
                                 bool addressScan) {
  return clientFromStorage(_nimbleClientStorage)
      .startScan(mac, forceRestart, interval, window, addressScan);
}

bool EspressoScaleBLE::pollScan() {
  return clientFromStorage(_nimbleClientStorage).poll();
}

bool EspressoScaleBLE::isScanning() const {
  return clientFromStorage(_nimbleClientStorage).isScanning();
}

bool EspressoScaleBLE::isConnecting() const {
  return clientFromStorage(_nimbleClientStorage).isConnecting();
}

void EspressoScaleBLE::disconnect() {
  clientFromStorage(_nimbleClientStorage).disconnect();
}

ScaleCommandResult EspressoScaleBLE::tare() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::Tare);
}

ScaleCommandResult EspressoScaleBLE::startTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::StartTimer);
}

ScaleCommandResult EspressoScaleBLE::stopTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::StopTimer);
}

ScaleCommandResult EspressoScaleBLE::resetTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::ResetTimer);
}

ScaleCommandResult EspressoScaleBLE::tareStartTimer() {
  if (!supportsTareStartTimer()) {
    return ScaleCommandResult::Unsupported;
  }
  return clientFromStorage(_nimbleClientStorage)
      .writeOp(ScaleOp::CombinedTareStart);
}

bool EspressoScaleBLE::supportsTareStartTimer() const {
  return features().has(ScaleFeatureCombinedTareStart);
}

ScaleCommandResult EspressoScaleBLE::beep() { return beepWithoutStateChange(); }

bool EspressoScaleBLE::supportsIndependentBeep() const {
  return features().has(ScaleFeatureIndependentBeep);
}

bool EspressoScaleBLE::supportsCommandFeedback() const {
  return features().has(ScaleFeatureCommandAudibleFeedback);
}

ScaleCommandResult EspressoScaleBLE::beepWithoutStateChange() {
  return setBeepLevel(1);
}

ScaleCommandResult EspressoScaleBLE::setBeepLevel(uint8_t level) {
  const ScaleFeatureSet available = features();
  if (!available.has(ScaleFeatureVolume) &&
      !available.has(ScaleFeatureIndependentBeep)) {
    return ScaleCommandResult::Unsupported;
  }
  if (level > available.volumeMax) {
    return ScaleCommandResult::InvalidArgument;
  }
  return clientFromStorage(_nimbleClientStorage)
      .writeOp(ScaleOp::SetVolume, level);
}

ScaleCommandResult EspressoScaleBLE::heartbeat() {
  NimbleScaleClient &client = clientFromStorage(_nimbleClientStorage);
  if (!client.features().has(ScaleFeatureHeartbeat)) {
    return ScaleCommandResult::Unsupported;
  }
  const ScaleCommandResult result = client.writeOp(ScaleOp::Heartbeat);
  if (scaleCommandOk(result)) {
    client.noteHeartbeat();
  }
  return result;
}

float EspressoScaleBLE::getWeight() const {
  return clientFromStorage(_nimbleClientStorage).weight();
}

bool EspressoScaleBLE::hasTimer() const {
  return clientFromStorage(_nimbleClientStorage).hasTimer();
}

uint32_t EspressoScaleBLE::getTimerMs() const {
  return clientFromStorage(_nimbleClientStorage).timerMs();
}

uint32_t EspressoScaleBLE::lastTimerAgeMs() const {
  return clientFromStorage(_nimbleClientStorage).timerAgeMs();
}

bool EspressoScaleBLE::heartbeatRequired() const {
  return clientFromStorage(_nimbleClientStorage).heartbeatRequired();
}

bool EspressoScaleBLE::isConnected() {
  return clientFromStorage(_nimbleClientStorage).isConnected();
}

bool EspressoScaleBLE::isLinkUp() const {
  return clientFromStorage(_nimbleClientStorage).isLinkUp();
}

bool EspressoScaleBLE::newWeightAvailable() {
  return clientFromStorage(_nimbleClientStorage).newWeightAvailable();
}

ScaleFeatureSet EspressoScaleBLE::features() const {
  return clientFromStorage(_nimbleClientStorage).features();
}

const char *EspressoScaleBLE::connectedProtocolName() const {
  return clientFromStorage(_nimbleClientStorage).protocolName();
}

const char *EspressoScaleBLE::address() const {
  return clientFromStorage(_nimbleClientStorage).address();
}

const char *EspressoScaleBLE::localName() const {
  return clientFromStorage(_nimbleClientStorage).name();
}

bool EspressoScaleBLE::isDirectedScan() const {
  return clientFromStorage(_nimbleClientStorage).directedScan();
}

bool EspressoScaleBLE::takeSeenAdvertisement(char *macOut, size_t macCapacity,
                                             char *nameOut,
                                             size_t nameCapacity) {
  return clientFromStorage(_nimbleClientStorage)
      .takeSeenAdvertisement(macOut, macCapacity, nameOut, nameCapacity);
}

ScaleDisconnectReason EspressoScaleBLE::lastDisconnectReason() const {
  return clientFromStorage(_nimbleClientStorage).lastReason();
}

const char *EspressoScaleBLE::lastDisconnectReasonName() const {
  return disconnectReasonName(lastDisconnectReason());
}

uint8_t EspressoScaleBLE::connectAttemptCount() const {
  return clientFromStorage(_nimbleClientStorage).connectAttempts();
}

uint8_t EspressoScaleBLE::connectStepId() const {
  return clientFromStorage(_nimbleClientStorage).stateId();
}

uint32_t EspressoScaleBLE::lastValidPacketAgeMs() const {
  return clientFromStorage(_nimbleClientStorage).lastPacketAgeMs();
}

uint32_t EspressoScaleBLE::rejectedPacketCount() const {
  return clientFromStorage(_nimbleClientStorage).rejectedPackets();
}

uint32_t EspressoScaleBLE::reconnectCount() const {
  return clientFromStorage(_nimbleClientStorage).reconnects();
}

ScaleBleTimingSnapshot EspressoScaleBLE::timingSnapshot() const {
  return clientFromStorage(_nimbleClientStorage).timing();
}

int32_t EspressoScaleBLE::lastBackendStatus() const {
  return clientFromStorage(_nimbleClientStorage).lastRawStatus();
}

ScaleBleBackendHealth EspressoScaleBLE::backendHealth() const {
  return clientFromStorage(_nimbleClientStorage).health();
}

int EspressoScaleBLE::linkRssi() {
  return clientFromStorage(_nimbleClientStorage).rssi();
}
