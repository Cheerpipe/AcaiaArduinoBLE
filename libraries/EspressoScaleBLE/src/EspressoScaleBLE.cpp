/*
  EspressoScaleBLE.cpp - ArduinoBLE gateway for espresso scales.
*/
#include "Arduino.h"
#include "EspressoScaleBLE.h"
#include <ArduinoBLE.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(ESP32) && !defined(SHOT_STOPPER_HOST_TEST)
#ifdef LOG_LOCAL_LEVEL
#undef LOG_LOCAL_LEVEL
#endif
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include <esp_log.h>

// The firmware provides this bridge when EspressoScaleBLE is linked into Shot
// Stopper. Keep it weak so the library remains usable by other projects.
extern "C" void shotStopperScaleLog(uint8_t severity, const char *message)
    __attribute__((weak));
#endif

#if defined(ESP32) && __has_include("esp32-hal-alloc-ble-mem.h")
#include "esp32-hal-alloc-ble-mem.h"
#endif

#if defined(ESP32) && !defined(SHOT_STOPPER_HOST_TEST)
extern "C" uint8_t BLEHostLastHciDisconnectReason(void);
#endif

namespace {

enum class ScaleLogSeverity : uint8_t {
    Debug = 0,
    Warning = 1,
    Error = 2,
};

constexpr size_t SCALE_LOG_LINE_CAPACITY = 128;
constexpr size_t SCALE_LOG_HEX_CAPACITY = 65;

void scaleLog(ScaleLogSeverity severity, const char *message) {
    if (message == nullptr) {
        return;
    }
#if defined(ESP32) && !defined(SHOT_STOPPER_HOST_TEST)
    if (shotStopperScaleLog != nullptr) {
        shotStopperScaleLog(static_cast<uint8_t>(severity), message);
        return;
    }
    switch (severity) {
        case ScaleLogSeverity::Error:
            ESP_LOGE("scale.ble", "%s", message);
            return;
        case ScaleLogSeverity::Warning:
            ESP_LOGW("scale.ble", "%s", message);
            return;
        case ScaleLogSeverity::Debug:
            ESP_LOGD("scale.ble", "%s", message);
            return;
    }
#else
    (void)severity;
    Serial.println(message);
#endif
}

void scaleLogf(ScaleLogSeverity severity, const char *format, ...) {
    if (format == nullptr) {
        return;
    }
    // Per-call storage keeps the logger re-entrant without a shared heap or
    // BSS accumulator. The line bound matches Shot Stopper's retained record.
    char line[SCALE_LOG_LINE_CAPACITY] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    scaleLog(severity, line);
}

void formatHexData(const unsigned char data[], int length, char *output,
                   size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    output[0] = '\0';
    if (data == nullptr || length <= 0) {
        return;
    }
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    size_t used = 0;
    for (int index = 0; index < length && used + 2 < capacity; ++index) {
        const unsigned char value = data[index];
        output[used++] = HEX_DIGITS[value >> 4];
        output[used++] = HEX_DIGITS[value & 0x0f];
    }
    output[used] = '\0';
}

uint32_t elapsedSince(uint32_t timestamp) {
    return static_cast<uint32_t>(millis()) - timestamp;
}

bool macAddressEqual(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (size_t i = 0; i < SCALE_MAC_CAPACITY; ++i) {
        unsigned char a = static_cast<unsigned char>(left[i]);
        unsigned char b = static_cast<unsigned char>(right[i]);
        if (a >= 'a' && a <= 'z') {
            a = static_cast<unsigned char>(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = static_cast<unsigned char>(b - 'a' + 'A');
        }
        if (a != b) {
            return false;
        }
        if (a == '\0') {
            return true;
        }
    }
    return false;
}

} // namespace

bool advertisedUuidLooksLikeScale(const BLEDevice &peripheral) {
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        const ScaleProtocol *protocol = scaleProtocolAt(i);
        if (protocol == 0) {
            continue;
        }
        uint16_t parsed = 0;
        if (scaleParseUuid16(protocol->readUuid, &parsed) &&
            scaleUuid16AllowsNamelessConnect(parsed) &&
            peripheral.hasAdvertisedUuid16(parsed)) {
            return true;
        }
        if (scaleParseUuid16(protocol->writeUuid, &parsed) &&
            scaleUuid16AllowsNamelessConnect(parsed) &&
            peripheral.hasAdvertisedUuid16(parsed)) {
            return true;
        }
    }
    return false;
}

EspressoScaleBLE::EspressoScaleBLE(bool debug) :
    _currentWeight(0.0f),
    _currentTimerMs(0),
    _lastTimerPacket(0),
    _hasTimer(false),
    _write(),
    _read(),
    _peripheral(),
    _lastHeartBeat(0),
    _connectedAt(0),
    _lastPacket(0),
    _packetPeriod(0),
    _rejectedPackets(0),
    _consecutiveRejectedPackets(0),
    _reconnects(0),
    _successfulConnections(0),
    _hasPeripheral(false),
    _hasValidPacket(false),
    _scanStartedAt(0),
    _scanning(false),
    _connected(false),
    _connecting(false),
    _loggedVersion(false),
    _protocol(0),
    _debug(debug),
    _connectStep(ConnectStep::Idle),
    _connectStartedAt(0),
    _connectSettleStartedAt(0),
    _connectAttempts(0),
    _linkDownSince(0),
    _scanMac{},
    _scanAddressFilter(false),
    _scanInterval(0),
    _scanWindow(0),
    _address{},
    _localName{},
    _seenMac{},
    _seenName{},
    _seenPending(false),
    _timingSnapshot{0, 0, 0, 0, 0},
    _lastDisconnectReason(ScaleDisconnectReason::NONE) {
}

EspressoScaleBLE::~EspressoScaleBLE() {
    resetConnection(true, ScaleDisconnectReason::NONE);
}

void EspressoScaleBLE::logVersionOnce() {
    if (_loggedVersion) {
        return;
    }
    _loggedVersion = true;
    if (_debug) {
        scaleLogf(ScaleLogSeverity::Debug,
                  "EspressoScaleBLE Library v%s ready", LIBRARY_VERSION);
    }
}

void EspressoScaleBLE::stopIdleScan(ScaleDisconnectReason reason) {
    if (_scanning) {
        BLE.stopScan();
        _scanning = false;
    }
    _connected = false;
    _scanStartedAt = 0;
    _scanMac[0] = '\0';
    _scanAddressFilter = false;
    if (reason != ScaleDisconnectReason::NONE) {
        _lastDisconnectReason = reason;
    }
}

bool EspressoScaleBLE::startScan(const char *mac, bool forceRestart,
                                 uint16_t interval, uint16_t window,
                                 bool addressScan) {
    logVersionOnce();

    if (window == 0 || window > interval) {
        interval = BLE_SCAN_NORMAL_INTERVAL;
        window = BLE_SCAN_NORMAL_WINDOW;
    }

    const bool filtered = mac != nullptr && mac[0] != '\0';
    const bool useAddressScan = filtered && addressScan;
    if (_scanning && !_connected && !_hasPeripheral) {
        const bool sameFilter = filtered
            ? macAddressEqual(_scanMac, mac)
            : _scanMac[0] == '\0';
        const bool sameHci =
            _scanInterval == interval && _scanWindow == window;
        const bool sameScanKind = _scanAddressFilter == useAddressScan;
        if (sameFilter && sameHci && sameScanKind && !forceRestart) {
            return true;
        }
        stopIdleScan(ScaleDisconnectReason::NONE);
    }

    if (_connected || _hasPeripheral || _scanning || _connecting) {
        resetConnection(true, ScaleDisconnectReason::NONE);
    }

    BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
    BLE.setScanParameters(interval, window);
    _scanInterval = interval;
    _scanWindow = window;
    _scanAddressFilter = useAddressScan;
    _seenPending = false;
    _seenMac[0] = '\0';
    _seenName[0] = '\0';
    _timingSnapshot = {0, 0, 0, 0, 0};
    if (filtered) {
        strncpy(_scanMac, mac, sizeof(_scanMac) - 1);
        _scanMac[sizeof(_scanMac) - 1] = '\0';
    } else {
        _scanMac[0] = '\0';
    }
    if (_debug) {
        if (!filtered) {
            scaleLog(ScaleLogSeverity::Debug,
                     "Scanning for any compatible scale (name scan)...");
        } else if (useAddressScan) {
            scaleLogf(ScaleLogSeverity::Debug,
                      "Scanning for preferred scale %s (address scan)...",
                      _scanMac);
        } else {
            scaleLogf(
                ScaleLogSeverity::Debug,
                "Scanning for preferred scale %s (name scan + connect filter)...",
                _scanMac);
        }
    }

    const bool scanStarted = useAddressScan
        ? static_cast<bool>(BLE.scanForAddress(mac, true))
        : static_cast<bool>(BLE.scan(true));
    if (!scanStarted) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "BLE scan failed to start");
        }
        stopIdleScan(ScaleDisconnectReason::SCAN_START_FAILED);
        return false;
    }
    _scanning = true;
    _scanStartedAt = static_cast<uint32_t>(millis());
    _timingSnapshot.scanStartedMs = _scanStartedAt;
    _timingSnapshot.recordedFlags |= ScaleBleTimingScanStarted;
    return true;
}

bool EspressoScaleBLE::isScanning() const {
    return _scanning;
}

bool EspressoScaleBLE::isConnecting() const {
    return _connecting;
}

uint32_t EspressoScaleBLE::maxPacketPeriodMs() const {
    if (_protocol != 0 && _protocol->features.maxPacketSilenceMs != 0) {
        return _protocol->features.maxPacketSilenceMs;
    }
    return MAX_PACKET_PERIOD_MS;
}

void EspressoScaleBLE::clearConnectingState() {
    _connecting = false;
    _connectStep = ConnectStep::Idle;
    _connectStartedAt = 0;
    _connectSettleStartedAt = 0;
}

bool EspressoScaleBLE::takeSeenAdvertisement(char *macOut, size_t macCapacity,
                                             char *nameOut, size_t nameCapacity) {
    if (!_seenPending) {
        return false;
    }
    if (macOut != nullptr && macCapacity > 0) {
        strncpy(macOut, _seenMac, macCapacity - 1);
        macOut[macCapacity - 1] = '\0';
    }
    if (nameOut != nullptr && nameCapacity > 0) {
        strncpy(nameOut, _seenName, nameCapacity - 1);
        nameOut[nameCapacity - 1] = '\0';
    }
    _seenPending = false;
    return true;
}

bool EspressoScaleBLE::pollScan() {
    if (_connected) {
        return true;
    }
    if (_connecting) {
        return advanceConnection();
    }
    if (!_scanning) {
        return false;
    }

    BLEDevice peripheral = BLE.available();
    if (peripheral) {
        char mac[ACAIA_MAC_CAPACITY] = {};
        char name[ACAIA_NAME_CAPACITY] = {};
        peripheral.copyAddress(mac, sizeof(mac));
        peripheral.copyLocalName(name, sizeof(name));

        if (_debug) {
            scaleLogf(ScaleLogSeverity::Debug, "Found %s '%s'", mac, name);
        }

        const bool filtered = _scanMac[0] != '\0';
        const bool nameOk = isScaleName(name);
        const bool uuidOk = advertisedUuidLooksLikeScale(peripheral);
        const bool macOk = filtered && macAddressEqual(mac, _scanMac);
        const bool compatibleAd = nameOk || uuidOk;

        if (compatibleAd || macOk) {
            if (!_timingSnapshot.has(
                    ScaleBleTimingFirstCompatibleAdvertisement)) {
                _timingSnapshot.firstCompatibleAdvertisementMs =
                    static_cast<uint32_t>(millis());
                _timingSnapshot.recordedFlags |=
                    ScaleBleTimingFirstCompatibleAdvertisement;
            }
        }

        if (compatibleAd) {
            strncpy(_seenMac, mac, sizeof(_seenMac) - 1);
            _seenMac[sizeof(_seenMac) - 1] = '\0';
            strncpy(_seenName, name, sizeof(_seenName) - 1);
            _seenName[sizeof(_seenName) - 1] = '\0';
            _seenPending = true;
        }

        if ((!filtered && compatibleAd) || macOk) {
            rememberPeripheral(peripheral);
            BLE.stopScan();
            _scanning = false;
            _scanStartedAt = 0;
            return beginConnection(_peripheral);
        }
    }

    return false;
}

bool EspressoScaleBLE::beginConnection(BLEDevice& peripheral) {
    if (_debug) {
        scaleLog(ScaleLogSeverity::Debug, "Connecting ...");
    }
    if (!_hasPeripheral) {
        rememberPeripheral(peripheral);
    }
    _connecting = true;
    _connectStep = ConnectStep::Settle;
    _connectStartedAt = static_cast<uint32_t>(millis());
    _connectSettleStartedAt = _connectStartedAt;
    _connectAttempts = 0;
    return false;
}

bool EspressoScaleBLE::advanceConnection() {
    if (!_connecting) {
        return _connected;
    }
    if (elapsedSince(_connectStartedAt) >= SCALE_CONNECT_BUDGET_MS) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "Scale connect budget exceeded");
        }
        BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
        resetConnection(true, ScaleDisconnectReason::CONNECT_FAILED);
        return false;
    }

    switch (_connectStep) {
        case ConnectStep::Settle:
            BLE.poll();
            if (elapsedSince(_connectSettleStartedAt) < SCALE_CONNECT_SETTLE_MS) {
                return false;
            }
            _connectStep = ConnectStep::Connect;
            return false;

        case ConnectStep::Connect:
            BLE.setTimeout(BLE_CONNECT_TIMEOUT_MS);
            if (!_timingSnapshot.has(ScaleBleTimingConnectIssued)) {
                _timingSnapshot.connectIssuedMs =
                    static_cast<uint32_t>(millis());
                _timingSnapshot.recordedFlags |=
                    ScaleBleTimingConnectIssued;
            }
            if (!_peripheral.connect()) {
                ++_connectAttempts;
                if (_debug) {
                    scaleLogf(ScaleLogSeverity::Warning,
                              "Failed to connect (attempt %u)",
                              static_cast<unsigned>(_connectAttempts));
                }
                if (_connectAttempts < SCALE_CONNECT_ATTEMPTS) {
                    BLE.poll();
                    _connectSettleStartedAt = static_cast<uint32_t>(millis());
                    _connectStep = ConnectStep::Settle;
                    return false;
                }
                BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
                resetConnection(true, ScaleDisconnectReason::CONNECT_FAILED);
                return false;
            }
            BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
            if (_debug) {
                scaleLog(ScaleLogSeverity::Debug, "Connected");
                scaleLog(ScaleLogSeverity::Debug,
                         "Discovering attributes ...");
            }
            _connectStep = ConnectStep::Discover;
            return false;

        case ConnectStep::Discover:
            BLE.setTimeout(BLE_DISCOVER_TIMEOUT_MS);
            if (!_peripheral.discoverAttributes()) {
                BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
                if (_debug) {
                    scaleLog(ScaleLogSeverity::Warning,
                             "Attribute discovery failed!");
                }
                resetConnection(true, ScaleDisconnectReason::DISCOVERY_FAILED);
                return false;
            }
            BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
            if (_debug) {
                scaleLog(ScaleLogSeverity::Debug, "Attributes discovered");
                scaleLogf(ScaleLogSeverity::Debug, "Device name: %s",
                          _peripheral.deviceName().c_str());
                scaleLogf(ScaleLogSeverity::Debug, "Appearance: 0x%X",
                          static_cast<unsigned>(_peripheral.appearance()));
                for (int i = 0; i < _peripheral.serviceCount(); ++i) {
                    exploreService(_peripheral.service(i));
                }
            }
            _connectStep = ConnectStep::Configure;
            return false;

        case ConnectStep::Configure:
            if (!detectAndConfigureScale()) {
                if (_debug) {
                    scaleLog(
                        ScaleLogSeverity::Warning,
                        "Unable to determine scale type or capabilities");
                }
                resetConnection(true, ScaleDisconnectReason::UNSUPPORTED_SCALE);
                return false;
            }
            _connectStep = ConnectStep::Subscribe;
            return false;

        case ConnectStep::Subscribe:
            if (!_read.subscribe()) {
                if (_debug) {
                    scaleLog(ScaleLogSeverity::Warning,
                             "Subscription failed");
                }
                resetConnection(true, ScaleDisconnectReason::SUBSCRIBE_FAILED);
                return false;
            }
            if (_debug) {
                scaleLog(ScaleLogSeverity::Debug, "Subscribed");
            }
            _connectStep = ConnectStep::InitWrites;
            return false;

        case ConnectStep::InitWrites:
            if (!runInitWrites()) {
                return false;
            }
            return finishConnectionSuccess();

        case ConnectStep::Idle:
        default:
            clearConnectingState();
            return false;
    }
}

bool EspressoScaleBLE::detectAndConfigureScale() {
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        const ScaleProtocol *protocol = scaleProtocolAt(i);
        if (protocol->requireAdvertisedName &&
            !scaleNameMatchesProtocol(_localName, protocol)) {
            continue;
        }
        BLECharacteristic candidate =
            _peripheral.characteristic(protocol->readUuid);
        if (candidate && candidate.canSubscribe()) {
            if (_debug) {
                scaleLog(ScaleLogSeverity::Debug, protocol->debugLabel);
            }
            if (configureCharacteristics(_peripheral, protocol)) {
                return true;
            }
        }
    }
    return false;
}

bool EspressoScaleBLE::runInitWrites() {
    if (_protocol == 0) {
        return true;
    }
    for (size_t i = 0; i < _protocol->initWriteCount; ++i) {
        const ScalePayload &payload = _protocol->initWrites[i];
        if (!_write.writeValue(payload.data, payload.length)) {
            if (_debug) {
                scaleLog(ScaleLogSeverity::Warning,
                         "Initialization write failed");
            }
            resetConnection(
                true, ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED);
            return false;
        }
    }
    return true;
}

bool EspressoScaleBLE::finishConnectionSuccess() {
    const uint32_t now = static_cast<uint32_t>(millis());
    clearConnectingState();
    _connected = true;
    _connectedAt = now;
    _timingSnapshot.readyMs = now;
    _timingSnapshot.recordedFlags |= ScaleBleTimingReady;
    _linkDownSince = 0;
    uint16_t heartbeatPeriod = HEARTBEAT_PERIOD_MS;
    if (_protocol != 0 && _protocol->features.heartbeatPeriodMs != 0) {
        heartbeatPeriod = _protocol->features.heartbeatPeriodMs;
    }
    _lastHeartBeat = now - heartbeatPeriod;
    _lastPacket = 0;
    _packetPeriod = 0;
    _hasValidPacket = false;
    _hasTimer = false;
    _currentTimerMs = 0;
    _lastTimerPacket = 0;
    if (_successfulConnections > 0) {
        ++_reconnects;
    }
    ++_successfulConnections;
    return true;
}

bool EspressoScaleBLE::init(const char *mac) {
    logVersionOnce();
    resetConnection(true, ScaleDisconnectReason::NONE);
    if (!startScan(mac)) {
        return false;
    }

    const uint32_t startedAt = static_cast<uint32_t>(millis());
    do {
        if (pollScan()) {
            return true;
        }
        if (!_scanning && !_connecting) {
            if (_lastDisconnectReason == ScaleDisconnectReason::SCAN_TIMEOUT) {
                if (_debug) {
                    scaleLog(ScaleLogSeverity::Warning,
                             "Scale scan timed out");
                }
            }
            return false;
        }
        delay(1);
        BLE.poll();
    } while (elapsedSince(startedAt) < SCALE_SCAN_TIMEOUT_MS || _connecting);

    if (_connecting) {
        const uint32_t connectDeadline =
            _connectStartedAt + SCALE_CONNECT_BUDGET_MS;
        while (_connecting &&
               static_cast<uint32_t>(millis()) < connectDeadline) {
            if (advanceConnection()) {
                return true;
            }
            delay(1);
            BLE.poll();
        }
        if (!_connected) {
            resetConnection(true, ScaleDisconnectReason::CONNECT_FAILED);
            return false;
        }
        return true;
    }

    if (_debug) {
        scaleLog(ScaleLogSeverity::Warning, "Scale scan timed out");
    }
    stopIdleScan(ScaleDisconnectReason::SCAN_TIMEOUT);
    return false;
}

void EspressoScaleBLE::disconnect() {
    resetConnection(true, ScaleDisconnectReason::USER_REQUEST);
}

bool EspressoScaleBLE::configureCharacteristics(BLEDevice& peripheral,
                                                const ScaleProtocol *protocol) {
    BLECharacteristic writeCandidate =
        peripheral.characteristic(protocol->writeUuid);
    BLECharacteristic readCandidate =
        peripheral.characteristic(protocol->readUuid);
    if (!writeCandidate || !readCandidate || !writeCandidate.canWrite() ||
        !readCandidate.canSubscribe()) {
        return false;
    }

    retainCharacteristic(_write, writeCandidate);
    retainCharacteristic(_read, readCandidate);
    _protocol = protocol;
    return true;
}

ScaleFeatureSet EspressoScaleBLE::features() const {
    if (!_connected || _protocol == 0) {
        return scaleFeatureSetNone();
    }
    return _protocol->features;
}

ScaleCommandResult EspressoScaleBLE::writeOp(ScaleOp op, uint8_t arg) {
    if (!isConnected()) {
        return ScaleCommandResult::NotConnected;
    }
    if (_protocol == 0 || _protocol->encodeCommand == 0) {
        return ScaleCommandResult::Unsupported;
    }
    uint8_t command[SCALE_MAX_COMMAND_LENGTH];
    int length = 0;
    if (!_protocol->encodeCommand(op, arg, command, &length) || length <= 0) {
        return ScaleCommandResult::Unsupported;
    }
    return writeCommand(command, length);
}

ScaleCommandResult EspressoScaleBLE::tare() {
    const ScaleCommandResult result = writeOp(ScaleOp::Tare);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result) ? "Tare write successful"
                                        : "Tare write failed");
    }
    return result;
}

ScaleCommandResult EspressoScaleBLE::startTimer() {
    const ScaleCommandResult result = writeOp(ScaleOp::StartTimer);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result) ? "Start timer write successful"
                                        : "Start timer write failed");
    }
    return result;
}

ScaleCommandResult EspressoScaleBLE::stopTimer() {
    const ScaleCommandResult result = writeOp(ScaleOp::StopTimer);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result) ? "Stop timer write successful"
                                        : "Stop timer write failed");
    }
    return result;
}

ScaleCommandResult EspressoScaleBLE::resetTimer() {
    const ScaleCommandResult result = writeOp(ScaleOp::ResetTimer);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result) ? "Reset timer write successful"
                                        : "Reset timer write failed");
    }
    return result;
}

ScaleCommandResult EspressoScaleBLE::tareStartTimer() {
    if (!supportsTareStartTimer()) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "Tare-and-start unsupported for this scale");
        }
        return ScaleCommandResult::Unsupported;
    }
    const ScaleCommandResult result = writeOp(ScaleOp::CombinedTareStart);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result)
                     ? "Tare-and-start write successful"
                     : "Tare-and-start write failed");
    }
    return result;
}

bool EspressoScaleBLE::supportsTareStartTimer() const {
    return features().has(ScaleFeatureCombinedTareStart);
}

ScaleCommandResult EspressoScaleBLE::beep() {
    return beepWithoutStateChange();
}

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
    if (!features().has(ScaleFeatureVolume) &&
        !features().has(ScaleFeatureIndependentBeep)) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "Beep level unsupported for this scale");
        }
        return ScaleCommandResult::Unsupported;
    }
    const uint8_t maxLevel = features().volumeMax;
    if (level > maxLevel) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "Beep level out of range");
        }
        return ScaleCommandResult::InvalidArgument;
    }
    const ScaleCommandResult result = writeOp(ScaleOp::SetVolume, level);
    if (_debug) {
        scaleLog(scaleCommandOk(result) ? ScaleLogSeverity::Debug
                                        : ScaleLogSeverity::Warning,
                 scaleCommandOk(result) ? "Beep level write successful"
                                        : "Beep level write failed");
    }
    return result;
}

ScaleCommandResult EspressoScaleBLE::heartbeat() {
    if (!features().has(ScaleFeatureHeartbeat)) {
        return ScaleCommandResult::Unsupported;
    }
    const ScaleCommandResult result = writeOp(ScaleOp::Heartbeat);
    if (scaleCommandOk(result)) {
        _lastHeartBeat = static_cast<uint32_t>(millis());
    }
    return result;
}

float EspressoScaleBLE::getWeight() const {
    return _currentWeight;
}

bool EspressoScaleBLE::hasTimer() const {
    return _connected && _hasTimer;
}

uint32_t EspressoScaleBLE::getTimerMs() const {
    return _hasTimer ? _currentTimerMs : 0;
}

uint32_t EspressoScaleBLE::lastTimerAgeMs() const {
    return _hasTimer ? elapsedSince(_lastTimerPacket) : 0xffffffffUL;
}

bool EspressoScaleBLE::heartbeatRequired() const {
    if (!features().has(ScaleFeatureHeartbeat)) {
        return false;
    }
    uint16_t period = HEARTBEAT_PERIOD_MS;
    if (_protocol != 0 && _protocol->features.heartbeatPeriodMs != 0) {
        period = _protocol->features.heartbeatPeriodMs;
    }
    return elapsedSince(_lastHeartBeat) >= period;
}

bool EspressoScaleBLE::isConnected() {
    if (!_connected) {
        return false;
    }
    if (_hasPeripheral && _peripheral.connected()) {
        _linkDownSince = 0;
        return true;
    }
    if (_linkDownSince == 0) {
        _linkDownSince = static_cast<uint32_t>(millis());
        return true;
    }
    if (elapsedSince(_linkDownSince) < LINK_DOWN_DEBOUNCE_MS) {
        return true;
    }
    resetConnection(false, mapHciDisconnectReason());
    return false;
}

bool EspressoScaleBLE::isLinkUp() const {
    return _connected;
}

const char* EspressoScaleBLE::connectedProtocolName() const {
    if (!_connected) {
        return "none";
    }
    if (_protocol == 0 || _protocol->id == 0) {
        return "unknown";
    }
    return _protocol->id;
}

const char *EspressoScaleBLE::address() const {
    return _hasPeripheral ? _address : "";
}

const char *EspressoScaleBLE::localName() const {
    return _hasPeripheral ? _localName : "";
}

bool EspressoScaleBLE::isDirectedScan() const {
    return _scanning && _scanMac[0] != '\0';
}

bool EspressoScaleBLE::newWeightAvailable() {
    if (!_connected) {
        return false;
    }

    const uint32_t now = static_cast<uint32_t>(millis());
    if (!_hasValidPacket &&
        static_cast<uint32_t>(now - _connectedAt) >=
            FIRST_PACKET_TIMEOUT_MS) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "First scale packet timed out");
        }
        resetConnection(true,
                        ScaleDisconnectReason::FIRST_PACKET_TIMEOUT);
        return false;
    }
    if (_hasValidPacket &&
        static_cast<uint32_t>(now - _lastPacket) >= maxPacketPeriodMs()) {
        if (_debug) {
            scaleLog(ScaleLogSeverity::Warning,
                     "Scale packet timed out");
        }
        resetConnection(true, ScaleDisconnectReason::PACKET_TIMEOUT);
        return false;
    }

    if (!_read.valueUpdated()) {
        return false;
    }

    const int length = _read.valueLength();
    if (!supportedPacketLength(length) || length > MAX_BLE_PACKET_LENGTH) {
        rejectPacket("unsupported length");
        return false;
    }

    uint8_t input[MAX_BLE_PACKET_LENGTH] = {0};
    const int bytesRead = _read.readValue(input, MAX_BLE_PACKET_LENGTH);
    if (_debug) {
        char hex[(MAX_BLE_PACKET_LENGTH * 2) + 1] = {};
        formatHexData(input, bytesRead, hex, sizeof(hex));
        scaleLogf(ScaleLogSeverity::Debug, "%d: 0x%s", bytesRead, hex);
    }
    if (bytesRead != length) {
        rejectPacket("truncated read");
        return false;
    }

    float parsedWeight = 0.0f;
    uint32_t parsedTimerMs = 0;
    const bool hasWeight = parseWeightPacket(input, bytesRead, parsedWeight);
    const bool hasTimer = parseTimerPacket(input, bytesRead, parsedTimerMs);
    if (!hasWeight && !hasTimer) {
        rejectPacket("invalid frame");
        return false;
    }

    const uint32_t receivedAt = static_cast<uint32_t>(millis());
    if (_hasValidPacket) {
        _packetPeriod = static_cast<uint32_t>(receivedAt - _lastPacket);
    }
    _lastPacket = receivedAt;
    _hasValidPacket = true;
    _consecutiveRejectedPackets = 0;
    if (hasTimer) {
        _currentTimerMs = parsedTimerMs;
        _lastTimerPacket = receivedAt;
        _hasTimer = true;
    }
    if (!hasWeight) {
        return false;
    }
    _currentWeight = parsedWeight;
    return true;
}

bool EspressoScaleBLE::supportedPacketLength(int length) const {
    if (_protocol == 0 || _protocol->supportedPacketLength == 0) {
        return false;
    }
    return _protocol->supportedPacketLength(length);
}

bool EspressoScaleBLE::parseWeightPacket(const uint8_t data[], int length,
                                         float& weight) const {
    if (_protocol == 0 || _protocol->parseWeight == 0) {
        return false;
    }
    return _protocol->parseWeight(data, length, &weight);
}

bool EspressoScaleBLE::parseTimerPacket(const uint8_t data[], int length,
                                        uint32_t& timerMs) const {
    if (_protocol == 0 || _protocol->parseTimer == 0) {
        return false;
    }
    return _protocol->parseTimer(data, length, &timerMs);
}

ScaleCommandResult EspressoScaleBLE::writeCommand(const uint8_t command[],
                                                  int length) {
    if (!isConnected() || !_write || command == nullptr || length <= 0) {
        return ScaleCommandResult::NotConnected;
    }
    if (_write.writeValue(command, length)) {
        return ScaleCommandResult::Ok;
    }
    resetConnection(true, ScaleDisconnectReason::COMMAND_WRITE_FAILED);
    return ScaleCommandResult::WriteFailed;
}

void EspressoScaleBLE::retainCharacteristic(
        BLECharacteristic& destination,
        const BLECharacteristic& source) {
    destination.~BLECharacteristic();
    new (&destination) BLECharacteristic(source);
}

void EspressoScaleBLE::clearCharacteristic(
        BLECharacteristic& characteristic) {
    characteristic.~BLECharacteristic();
    new (&characteristic) BLECharacteristic();
}

void EspressoScaleBLE::rememberPeripheral(const BLEDevice& peripheral) {
    _peripheral.~BLEDevice();
    new (&_peripheral) BLEDevice(peripheral);
    _hasPeripheral = true;
    peripheral.copyAddress(_address, sizeof(_address));
    peripheral.copyLocalName(_localName, sizeof(_localName));
}

void EspressoScaleBLE::clearPeripheral() {
    _peripheral.~BLEDevice();
    new (&_peripheral) BLEDevice();
    _hasPeripheral = false;
    _address[0] = '\0';
    _localName[0] = '\0';
}

void EspressoScaleBLE::resetConnection(bool disconnectPeer,
                                       ScaleDisconnectReason reason) {
    _connected = false;
    clearConnectingState();
    if (reason != ScaleDisconnectReason::NONE) {
        _lastDisconnectReason = reason;
    }

    if (_scanning) {
        BLE.stopScan();
        _scanning = false;
    }
    _scanStartedAt = 0;
    _scanMac[0] = '\0';
    _scanAddressFilter = false;

    clearCharacteristic(_read);
    clearCharacteristic(_write);

    if (_hasPeripheral && disconnectPeer && _peripheral.connected()) {
        _peripheral.disconnect();
    }
    clearPeripheral();

    _connectedAt = 0;
    _linkDownSince = 0;
    _lastPacket = 0;
    _lastHeartBeat = 0;
    _packetPeriod = 0;
    _hasValidPacket = false;
    _consecutiveRejectedPackets = 0;
    _hasTimer = false;
    _currentTimerMs = 0;
    _lastTimerPacket = 0;
    _protocol = 0;
}

void EspressoScaleBLE::rejectPacket(const char* reason) {
    ++_rejectedPackets;
    ++_consecutiveRejectedPackets;
    if (_debug) {
        scaleLogf(ScaleLogSeverity::Warning, "Rejected scale packet: %s",
                  reason != nullptr ? reason : "unknown");
    }
    if (_connected &&
        _consecutiveRejectedPackets >= MAX_CONSECUTIVE_REJECTED_PACKETS) {
        if (_debug) {
            scaleLog(
                ScaleLogSeverity::Error,
                "Invalid scale packet stream; disconnecting (worker will rescan)");
        }
        resetConnection(true,
                        ScaleDisconnectReason::INVALID_PACKET_STREAM);
    }
}

ScaleDisconnectReason EspressoScaleBLE::mapHciDisconnectReason() const {
#if defined(ESP32) && !defined(SHOT_STOPPER_HOST_TEST)
    const uint8_t hci = BLEHostLastHciDisconnectReason();
    if (hci == 0x08) {
        return ScaleDisconnectReason::SUPERVISION_TIMEOUT;
    }
    if (hci == 0x3E) {
        return ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH;
    }
#endif
    return ScaleDisconnectReason::REMOTE_DISCONNECTED;
}

ScaleDisconnectReason EspressoScaleBLE::lastDisconnectReason() const {
    return _lastDisconnectReason;
}

const char* EspressoScaleBLE::lastDisconnectReasonName() const {
    switch (_lastDisconnectReason) {
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

uint8_t EspressoScaleBLE::connectAttemptCount() const {
    return _connectAttempts;
}

uint8_t EspressoScaleBLE::connectStepId() const {
    return static_cast<uint8_t>(_connectStep);
}

uint32_t EspressoScaleBLE::lastValidPacketAgeMs() const {
    return _hasValidPacket ? elapsedSince(_lastPacket) : 0xffffffffUL;
}

uint32_t EspressoScaleBLE::rejectedPacketCount() const {
    return _rejectedPackets;
}

uint32_t EspressoScaleBLE::reconnectCount() const {
    return _reconnects;
}

ScaleBleTimingSnapshot EspressoScaleBLE::timingSnapshot() const {
    return _timingSnapshot;
}

int32_t EspressoScaleBLE::lastBackendStatus() const {
    return 0;
}

ScaleBleBackendHealth EspressoScaleBLE::backendHealth() const {
    return {};
}

int EspressoScaleBLE::linkRssi() {
    if (!_connected || !_hasPeripheral) {
        return SCALE_LINK_RSSI_UNAVAILABLE;
    }
    return _peripheral.rssi();
}

bool EspressoScaleBLE::isScaleName(const char *name) const {
    return scaleNameIsCompatible(name);
}

void EspressoScaleBLE::exploreService(BLEService service) {
    scaleLogf(ScaleLogSeverity::Debug, "Service %s", service.uuid());
    for (int i = 0; i < service.characteristicCount(); ++i) {
        exploreCharacteristic(service.characteristic(i));
    }
}

void EspressoScaleBLE::exploreCharacteristic(
        BLECharacteristic characteristic) {
    char hex[SCALE_LOG_HEX_CAPACITY] = {};
    if (characteristic.canRead()) {
        characteristic.read();
        if (characteristic.valueLength() > 0) {
            formatHexData(characteristic.value(),
                          characteristic.valueLength(), hex, sizeof(hex));
        }
    }
    scaleLogf(ScaleLogSeverity::Debug,
              "  Characteristic %s, properties 0x%X%s%s",
              characteristic.uuid(),
              static_cast<unsigned>(characteristic.properties()),
              hex[0] != '\0' ? ", value 0x" : "", hex);

    for (int i = 0; i < characteristic.descriptorCount(); ++i) {
        exploreDescriptor(characteristic.descriptor(i));
    }
}

void EspressoScaleBLE::exploreDescriptor(BLEDescriptor descriptor) {
    descriptor.read();
    char hex[SCALE_LOG_HEX_CAPACITY] = {};
    formatHexData(descriptor.value(), descriptor.valueLength(), hex,
                  sizeof(hex));
    scaleLogf(ScaleLogSeverity::Debug, "    Descriptor %s, value 0x%s",
              descriptor.uuid(), hex);
}
