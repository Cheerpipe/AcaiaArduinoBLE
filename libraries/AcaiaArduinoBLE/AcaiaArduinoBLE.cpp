/*
  AcaiaArduinoBLE.cpp - ArduinoBLE gateway for Acaia-compatible scales.
*/
#include "Arduino.h"
#include "AcaiaArduinoBLE.h"
#include <ArduinoBLE.h>

#include <math.h>
#include <new>
#include <string.h>

namespace {

static const byte IDENTIFY[20] = {
    0xef, 0xdd, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x9a, 0x6d
};
static const byte HEARTBEAT[7] =
    {0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00};
static const byte NOTIFICATION_REQUEST[14] = {
    0xef, 0xdd, 0x0c, 0x09, 0x00, 0x01, 0x01,
    0x02, 0x02, 0x05, 0x03, 0x04, 0x15, 0x06
};
static const byte START_TIMER[7] =
    {0xef, 0xdd, 0x0d, 0x00, 0x00, 0x00, 0x00};
static const byte STOP_TIMER[7] =
    {0xef, 0xdd, 0x0d, 0x00, 0x02, 0x00, 0x02};
static const byte RESET_TIMER[7] =
    {0xef, 0xdd, 0x0d, 0x00, 0x01, 0x00, 0x01};
static const byte TARE_ACAIA[6] =
    {0xef, 0xdd, 0x04, 0x00, 0x00, 0x00};
static const byte TARE_GENERIC[6] =
    {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};
static const byte START_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a};
static const byte STOP_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d};
static const byte RESET_TIMER_GENERIC[6] =
    {0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c};
static const byte TARE_START_TIMER_BOOKOO[6] =
    {0x03, 0x0a, 0x07, 0x00, 0x00, 0x00};
static const byte TARE_FELICITA[1] = {0x54};
static const byte START_TIMER_FELICITA[1] = {0x52};
static const byte STOP_TIMER_FELICITA[1] = {0x53};
static const byte RESET_TIMER_FELICITA[1] = {0x43};
static const byte WEIGHT_TIMER_MODE_FELICITA[1] = {0x32};

static const int MAX_BLE_PACKET_LENGTH = 20;
static const byte GENERIC_PRODUCT = 0x03;
static const byte GENERIC_TYPE = 0x0a;
static const byte GENERIC_BEEP_LEVEL_CMD = 0x02;
static const uint8_t GENERIC_BEEP_LEVEL_MAX = 5;

void fillGenericCommand(byte out[6], byte data1, byte data2, byte data3) {
    out[0] = GENERIC_PRODUCT;
    out[1] = GENERIC_TYPE;
    out[2] = data1;
    out[3] = data2;
    out[4] = data3;
    out[5] = static_cast<byte>(out[0] ^ out[1] ^ out[2] ^ out[3] ^ out[4]);
}

uint32_t elapsedSince(uint32_t timestamp) {
    return static_cast<uint32_t>(millis()) - timestamp;
}

bool macAddressEqual(const char *left, const char *right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (;;) {
        unsigned char a = static_cast<unsigned char>(*left);
        unsigned char b = static_cast<unsigned char>(*right);
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
        ++left;
        ++right;
    }
}

float decimalDivisor(byte exponent) {
    static const float divisors[] = {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f};
    return divisors[exponent];
}

bool looksLikeAcaiaTimer(byte minutes, byte seconds, byte tenths) {
    return minutes <= 99 && seconds <= 59 && tenths <= 9;
}

uint32_t acaiaTimerToMs(byte minutes, byte seconds, byte tenths) {
    return (static_cast<uint32_t>(minutes) * 60UL + seconds) * 1000UL +
           static_cast<uint32_t>(tenths) * 100UL;
}

bool felicitaAsciiTimer(const byte data[], uint32_t& timerMs) {
    for (int i = 9; i <= 13; ++i) {
        if (data[i] < '0' || data[i] > '9') {
            return false;
        }
    }
    const byte minutes = static_cast<byte>((data[9] - '0') * 10 + (data[10] - '0'));
    const byte seconds = static_cast<byte>((data[11] - '0') * 10 + (data[12] - '0'));
    const byte tenths = static_cast<byte>(data[13] - '0');
    if (!looksLikeAcaiaTimer(minutes, seconds, tenths)) {
        return false;
    }
    timerMs = acaiaTimerToMs(minutes, seconds, tenths);
    return true;
}

} // namespace

AcaiaArduinoBLE::AcaiaArduinoBLE(bool debug) :
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
    _loggedVersion(false),
    _type(OLD),
    _debug(debug),
    _scanMac{},
    _address{},
    _localName{},
    _seenMac{},
    _seenName{},
    _seenPending(false),
    _lastDisconnectReason(AcaiaDisconnectReason::NONE) {
}

AcaiaArduinoBLE::~AcaiaArduinoBLE() {
    resetConnection(true, AcaiaDisconnectReason::NONE);
}

void AcaiaArduinoBLE::logVersionOnce() {
    if (_loggedVersion) {
        return;
    }
    _loggedVersion = true;
    Serial.print("AcaiaArduinoBLE Library v");
    Serial.print(LIBRARY_VERSION);
    Serial.println(" ready");
}

void AcaiaArduinoBLE::stopIdleScan(AcaiaDisconnectReason reason) {
    if (_scanning) {
        BLE.stopScan();
        _scanning = false;
    }
    _connected = false;
    _scanStartedAt = 0;
    _scanMac[0] = '\0';
    if (reason != AcaiaDisconnectReason::NONE) {
        _lastDisconnectReason = reason;
    }
}

bool AcaiaArduinoBLE::startScan(const char *mac, bool forceRestart) {
    logVersionOnce();

    const bool filtered = mac != nullptr && mac[0] != '\0';
    // An active GAP scan must not be restarted with the same filter.
    // ArduinoBLE/ESP32 HCI can drop the scanner if scan() is issued while
    // already enabled. A filter change (or forceRestart) stops first.
    if (_scanning && !_connected && !_hasPeripheral) {
        const bool sameFilter = filtered
            ? macAddressEqual(_scanMac, mac)
            : _scanMac[0] == '\0';
        if (sameFilter && !forceRestart) {
            return true;
        }
        stopIdleScan(AcaiaDisconnectReason::NONE);
    }

    if (_connected || _hasPeripheral || _scanning) {
        resetConnection(true, AcaiaDisconnectReason::NONE);
    }

    BLE.setTimeout(BLE_OPERATION_TIMEOUT_MS);
    _seenPending = false;
    _seenMac[0] = '\0';
    _seenName[0] = '\0';
    if (filtered) {
        strncpy(_scanMac, mac, sizeof(_scanMac) - 1);
        _scanMac[sizeof(_scanMac) - 1] = '\0';
    } else {
        _scanMac[0] = '\0';
    }
    if (_debug) {
        if (!filtered) {
            Serial.println("Scanning for any compatible scale (name scan)...");
        } else {
            Serial.print("Scanning for preferred scale ");
            Serial.print(_scanMac);
            Serial.println(" (name scan + connect filter)...");
        }
    }

    // Always name-scan so non-preferred compatible scales can be observed for
    // history while a connect filter is active. withDuplicates=true so a
    // missed first advertisement is not fatal while the idle scan stays on.
    const bool scanStarted = static_cast<bool>(BLE.scan(true));
    if (!scanStarted) {
        Serial.println("BLE scan failed to start");
        stopIdleScan(AcaiaDisconnectReason::SCAN_START_FAILED);
        return false;
    }
    _scanning = true;
    _scanStartedAt = static_cast<uint32_t>(millis());
    return true;
}

bool AcaiaArduinoBLE::isScanning() const {
    return _scanning;
}

bool AcaiaArduinoBLE::takeSeenAdvertisement(char *macOut, size_t macCapacity,
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

bool AcaiaArduinoBLE::pollScan() {
    if (_connected) {
        return true;
    }
    if (!_scanning) {
        return false;
    }

    BLEDevice peripheral = BLE.available();
    if (_debug && peripheral) {
        Serial.print("Found ");
        Serial.print(peripheral.address());
        Serial.print(" '");
        Serial.print(peripheral.localName());
        Serial.print("' ");
        Serial.print(peripheral.advertisedServiceUuid());
        Serial.println();
    }

    if (peripheral) {
        const bool filtered = _scanMac[0] != '\0';
        const bool nameOk = isScaleName(peripheral.localName().c_str());
        const bool macOk =
            filtered &&
            macAddressEqual(peripheral.address().c_str(), _scanMac);

        if (nameOk) {
            strncpy(_seenMac, peripheral.address().c_str(),
                    sizeof(_seenMac) - 1);
            _seenMac[sizeof(_seenMac) - 1] = '\0';
            strncpy(_seenName, peripheral.localName().c_str(),
                    sizeof(_seenName) - 1);
            _seenName[sizeof(_seenName) - 1] = '\0';
            _seenPending = true;
        }

        // Unfiltered: first compatible name. Filtered: matching MAC (name
        // optional — preferred scales may advertise briefly without a name).
        if ((!filtered && nameOk) || macOk) {
            BLE.stopScan();
            _scanning = false;
            _scanStartedAt = 0;
            return completeConnection(peripheral);
        }
    }

    return false;
}

bool AcaiaArduinoBLE::completeConnection(BLEDevice& peripheral) {
    Serial.println("Connecting ...");
    if (!peripheral.connect()) {
        Serial.println("Failed to connect!");
        resetConnection(false, AcaiaDisconnectReason::CONNECT_FAILED);
        return false;
    }
    rememberPeripheral(peripheral);
    Serial.println("Connected");

    Serial.println("Discovering attributes ...");
    if (!peripheral.discoverAttributes()) {
        Serial.println("Attribute discovery failed!");
        resetConnection(true, AcaiaDisconnectReason::DISCOVERY_FAILED);
        return false;
    }
    Serial.println("Attributes discovered");

    if (_debug) {
        Serial.println();
        Serial.print("Device name: ");
        Serial.println(peripheral.deviceName());
        Serial.print("Appearance: 0x");
        Serial.println(peripheral.appearance(), HEX);
        Serial.println();

        for (int i = 0; i < peripheral.serviceCount(); ++i) {
            exploreService(peripheral.service(i));
        }
    }

    bool configured = false;
    BLECharacteristic candidate =
        peripheral.characteristic(READ_CHAR_OLD_VERSION);
    if (candidate && candidate.canSubscribe()) {
        Serial.println("Old version Acaia detected");
        configured = configureCharacteristics(
            peripheral, OLD, WRITE_CHAR_OLD_VERSION,
            READ_CHAR_OLD_VERSION);
    } else {
        BLECharacteristic newCandidate =
            peripheral.characteristic(READ_CHAR_NEW_VERSION);
        if (newCandidate && newCandidate.canSubscribe()) {
            Serial.println("New version Acaia detected");
            configured = configureCharacteristics(
                peripheral, NEW, WRITE_CHAR_NEW_VERSION,
                READ_CHAR_NEW_VERSION);
        } else {
            BLECharacteristic genericCandidate =
                peripheral.characteristic(READ_CHAR_GENERIC);
            if (genericCandidate && genericCandidate.canSubscribe()) {
                Serial.println("Generic scale detected");
                configured = configureCharacteristics(
                    peripheral, GENERIC, WRITE_CHAR_GENERIC,
                    READ_CHAR_GENERIC);
            } else {
                BLECharacteristic felicitaCandidate =
                    peripheral.characteristic(READ_CHAR_FELICITA);
                if (felicitaCandidate &&
                    felicitaCandidate.canSubscribe()) {
                    Serial.println("Felicita Arc detected");
                    configured = configureCharacteristics(
                        peripheral, FELICITA, WRITE_CHAR_FELICITA,
                        READ_CHAR_FELICITA);
                }
            }
        }
    }

    if (!configured) {
        Serial.println("Unable to determine scale type or capabilities");
        resetConnection(true, AcaiaDisconnectReason::UNSUPPORTED_SCALE);
        return false;
    }

    if (!_read.subscribe()) {
        Serial.println("Subscription failed");
        resetConnection(true, AcaiaDisconnectReason::SUBSCRIBE_FAILED);
        return false;
    }
    Serial.println("Subscribed");

    if (_type == OLD || _type == NEW) {
        if (!_write.writeValue(IDENTIFY, sizeof(IDENTIFY))) {
            Serial.println("Identify write failed");
            resetConnection(
                true, AcaiaDisconnectReason::INITIALIZATION_WRITE_FAILED);
            return false;
        }
        if (!_write.writeValue(NOTIFICATION_REQUEST,
                               sizeof(NOTIFICATION_REQUEST))) {
            Serial.println("Notification request write failed");
            resetConnection(
                true, AcaiaDisconnectReason::INITIALIZATION_WRITE_FAILED);
            return false;
        }
    } else if (_type == FELICITA) {
        if (!_write.writeValue(WEIGHT_TIMER_MODE_FELICITA,
                               sizeof(WEIGHT_TIMER_MODE_FELICITA))) {
            Serial.println("Felicita mode write failed");
            resetConnection(
                true, AcaiaDisconnectReason::INITIALIZATION_WRITE_FAILED);
            return false;
        }
    }

    const uint32_t now = static_cast<uint32_t>(millis());
    _connected = true;
    _connectedAt = now;
    _lastHeartBeat = now - HEARTBEAT_PERIOD_MS;
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

bool AcaiaArduinoBLE::init(const char *mac) {
    logVersionOnce();
    resetConnection(true, AcaiaDisconnectReason::NONE);
    if (!startScan(mac)) {
        return false;
    }

    do {
        if (pollScan()) {
            return true;
        }
        if (!_scanning) {
            if (_lastDisconnectReason == AcaiaDisconnectReason::SCAN_TIMEOUT) {
                Serial.println("Scale scan timed out");
            }
            return false;
        }
        // Yield on unicore ESP32 targets while retaining the synchronous API.
        delay(1);
    } while (elapsedSince(_scanStartedAt) < SCALE_SCAN_TIMEOUT_MS);

    Serial.println("Scale scan timed out");
    stopIdleScan(AcaiaDisconnectReason::SCAN_TIMEOUT);
    return false;
}

void AcaiaArduinoBLE::disconnect() {
    resetConnection(true, AcaiaDisconnectReason::USER_REQUEST);
}

bool AcaiaArduinoBLE::configureCharacteristics(BLEDevice& peripheral,
                                                scale_type type,
                                                const char* writeUuid,
                                                const char* readUuid) {
    BLECharacteristic writeCandidate = peripheral.characteristic(writeUuid);
    BLECharacteristic readCandidate = peripheral.characteristic(readUuid);
    if (!writeCandidate || !readCandidate || !writeCandidate.canWrite() ||
        !readCandidate.canSubscribe()) {
        return false;
    }

    // Copy construction retains the remote attributes. Do not replace these
    // calls with BLECharacteristic::operator= while ArduinoBLE lacks a safe
    // assignment operator.
    retainCharacteristic(_write, writeCandidate);
    retainCharacteristic(_read, readCandidate);
    _type = type;
    return true;
}

bool AcaiaArduinoBLE::tare() {
    const byte* command = TARE_ACAIA;
    int length = sizeof(TARE_ACAIA);
    if (_type == GENERIC) {
        command = TARE_GENERIC;
        length = sizeof(TARE_GENERIC);
    } else if (_type == FELICITA) {
        command = TARE_FELICITA;
        length = sizeof(TARE_FELICITA);
    }

    const bool ok = writeCommand(command, length);
    Serial.println(ok ? "Tare write successful" : "Tare write failed");
    return ok;
}

bool AcaiaArduinoBLE::startTimer() {
    const byte* command = START_TIMER;
    int length = sizeof(START_TIMER);
    if (_type == GENERIC) {
        command = START_TIMER_GENERIC;
        length = sizeof(START_TIMER_GENERIC);
    } else if (_type == FELICITA) {
        command = START_TIMER_FELICITA;
        length = sizeof(START_TIMER_FELICITA);
    }

    const bool ok = writeCommand(command, length);
    Serial.println(ok ? "Start timer write successful"
                      : "Start timer write failed");
    return ok;
}

bool AcaiaArduinoBLE::stopTimer() {
    const byte* command = STOP_TIMER;
    int length = sizeof(STOP_TIMER);
    if (_type == GENERIC) {
        command = STOP_TIMER_GENERIC;
        length = sizeof(STOP_TIMER_GENERIC);
    } else if (_type == FELICITA) {
        command = STOP_TIMER_FELICITA;
        length = sizeof(STOP_TIMER_FELICITA);
    }

    const bool ok = writeCommand(command, length);
    Serial.println(ok ? "Stop timer write successful"
                      : "Stop timer write failed");
    return ok;
}

bool AcaiaArduinoBLE::resetTimer() {
    const byte* command = RESET_TIMER;
    int length = sizeof(RESET_TIMER);
    if (_type == GENERIC) {
        command = RESET_TIMER_GENERIC;
        length = sizeof(RESET_TIMER_GENERIC);
    } else if (_type == FELICITA) {
        command = RESET_TIMER_FELICITA;
        length = sizeof(RESET_TIMER_FELICITA);
    }

    const bool ok = writeCommand(command, length);
    Serial.println(ok ? "Reset timer write successful"
                      : "Reset timer write failed");
    return ok;
}

bool AcaiaArduinoBLE::tareStartTimer() {
    if (!supportsTareStartTimer()) {
        Serial.println("Tare-and-start unsupported for this scale");
        return false;
    }
    const bool ok = writeCommand(TARE_START_TIMER_BOOKOO,
                                 sizeof(TARE_START_TIMER_BOOKOO));
    Serial.println(ok ? "Tare-and-start write successful"
                      : "Tare-and-start write failed");
    return ok;
}

bool AcaiaArduinoBLE::supportsTareStartTimer() const {
    return _connected && _type == GENERIC;
}

bool AcaiaArduinoBLE::beep() {
    // Legacy entry point deliberately delegates to the command that cannot
    // tare or otherwise change the scale's measurement state.
    return beepWithoutStateChange();
}

bool AcaiaArduinoBLE::supportsIndependentBeep() const {
    return _connected && _type == GENERIC;
}

bool AcaiaArduinoBLE::beepWithoutStateChange() {
    return setBeepLevel(1);
}

bool AcaiaArduinoBLE::setBeepLevel(uint8_t level) {
    if (!supportsIndependentBeep()) {
        Serial.println("Beep level unsupported for this scale");
        return false;
    }
    if (level > GENERIC_BEEP_LEVEL_MAX) {
        Serial.println("Beep level out of range");
        return false;
    }
    byte command[6];
    fillGenericCommand(command, GENERIC_BEEP_LEVEL_CMD, 0x00, level);
    const bool ok = writeCommand(command, sizeof(command));
    Serial.println(ok ? "Beep level write successful"
                      : "Beep level write failed");
    return ok;
}

bool AcaiaArduinoBLE::heartbeat() {
    if ((_type != OLD && _type != NEW) || !isConnected()) {
        return false;
    }
    if (!writeCommand(HEARTBEAT, sizeof(HEARTBEAT))) {
        return false;
    }
    _lastHeartBeat = static_cast<uint32_t>(millis());
    return true;
}

float AcaiaArduinoBLE::getWeight() const {
    return _currentWeight;
}

bool AcaiaArduinoBLE::hasTimer() const {
    return _connected && _hasTimer;
}

uint32_t AcaiaArduinoBLE::getTimerMs() const {
    return _hasTimer ? _currentTimerMs : 0;
}

uint32_t AcaiaArduinoBLE::lastTimerAgeMs() const {
    return _hasTimer ? elapsedSince(_lastTimerPacket) : 0xffffffffUL;
}

bool AcaiaArduinoBLE::heartbeatRequired() const {
    return _connected && (_type == OLD || _type == NEW) &&
           elapsedSince(_lastHeartBeat) >= HEARTBEAT_PERIOD_MS;
}

bool AcaiaArduinoBLE::isConnected() {
    if (!_connected) {
        return false;
    }
    if (!_hasPeripheral || !_peripheral.connected()) {
        resetConnection(false, AcaiaDisconnectReason::REMOTE_DISCONNECTED);
        return false;
    }
    return true;
}

const char* AcaiaArduinoBLE::connectedProtocolName() const {
    if (!_connected) {
        return "none";
    }
    switch (_type) {
        case OLD: return "acaia_legacy";
        case NEW: return "acaia";
        case GENERIC: return "bookoo_generic";
        case FELICITA: return "felicita";
    }
    return "unknown";
}

const char *AcaiaArduinoBLE::address() const {
    return _hasPeripheral ? _address : "";
}

const char *AcaiaArduinoBLE::localName() const {
    return _hasPeripheral ? _localName : "";
}

bool AcaiaArduinoBLE::isDirectedScan() const {
    return _scanning && _scanMac[0] != '\0';
}

bool AcaiaArduinoBLE::newWeightAvailable() {
    if (!isConnected()) {
        return false;
    }

    const uint32_t now = static_cast<uint32_t>(millis());
    if (!_hasValidPacket &&
        static_cast<uint32_t>(now - _connectedAt) >=
            FIRST_PACKET_TIMEOUT_MS) {
        Serial.println("First scale packet timed out");
        resetConnection(true,
                        AcaiaDisconnectReason::FIRST_PACKET_TIMEOUT);
        return false;
    }
    if (_hasValidPacket &&
        static_cast<uint32_t>(now - _lastPacket) >= MAX_PACKET_PERIOD_MS) {
        Serial.println("Scale packet timed out");
        resetConnection(true, AcaiaDisconnectReason::PACKET_TIMEOUT);
        return false;
    }

    if (!_read.valueUpdated()) {
        return false;
    }

    const int length = _read.valueLength();
    if (!supportedPacketLength(length)) {
        rejectPacket("unsupported length");
        return false;
    }

    byte input[MAX_BLE_PACKET_LENGTH] = {0};
    const int bytesRead = _read.readValue(input, length);
    if (_debug) {
        Serial.print(bytesRead);
        Serial.print(": 0x");
        printData(input, bytesRead);
        Serial.println();
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

bool AcaiaArduinoBLE::supportedPacketLength(int length) const {
    switch (_type) {
        case OLD:
            return length == 10 || length == 14;
        case NEW:
            return length == 10 || length == 13 || length == 17;
        case GENERIC:
            return length == 20;
        case FELICITA:
            return length == 18;
    }
    return false;
}

bool AcaiaArduinoBLE::parseWeightPacket(const byte data[], int length,
                                         float& weight) const {
    switch (_type) {
        case OLD:
            return parseAcaiaOldPacket(data, length, weight);
        case NEW:
            return parseAcaiaNewPacket(data, length, weight);
        case GENERIC:
            return parseGenericPacket(data, length, weight);
        case FELICITA:
            return parseFelicitaPacket(data, length, weight);
    }
    return false;
}

bool AcaiaArduinoBLE::parseTimerPacket(const byte data[], int length,
                                        uint32_t& timerMs) const {
    switch (_type) {
        case NEW:
            if ((length != 10 && length != 13 && length != 17) ||
                data[0] != 0xef || data[1] != 0xdd || data[2] != 0x0c ||
                static_cast<int>(data[3]) + 5 != length ||
                !validAcaiaChecksum(data, length)) {
                return false;
            }
            if (data[4] == 0x07 &&
                looksLikeAcaiaTimer(data[5], data[6], data[7])) {
                timerMs = acaiaTimerToMs(data[5], data[6], data[7]);
                return true;
            }
            if (data[4] == 0x05 && length == 17 &&
                looksLikeAcaiaTimer(data[11], data[12], data[13])) {
                timerMs = acaiaTimerToMs(data[11], data[12], data[13]);
                return true;
            }
            return false;
        case GENERIC: {
            float ignoredWeight = 0.0f;
            if (!parseGenericPacket(data, length, ignoredWeight)) {
                return false;
            }
            timerMs = (static_cast<uint32_t>(data[2]) << 16) |
                      (static_cast<uint32_t>(data[3]) << 8) | data[4];
            return true;
        }
        case FELICITA: {
            float ignoredWeight = 0.0f;
            if (!parseFelicitaPacket(data, length, ignoredWeight)) {
                return false;
            }
            if (felicitaAsciiTimer(data, timerMs)) {
                return true;
            }
            if (looksLikeAcaiaTimer(data[9], data[10], data[11])) {
                timerMs = acaiaTimerToMs(data[9], data[10], data[11]);
                return true;
            }
            return false;
        }
        case OLD:
            break;
    }
    return false;
}

bool AcaiaArduinoBLE::parseAcaiaNewPacket(const byte data[], int length,
                                           float& weight) const {
    if ((length != 13 && length != 17) || data[0] != 0xef ||
        data[1] != 0xdd || data[2] != 0x0c ||
        static_cast<int>(data[3]) + 5 != length || data[4] != 0x05 ||
        data[9] < 1 || data[9] > 4 ||
        !validAcaiaChecksum(data, length)) {
        return false;
    }

    const uint32_t raw =
        (static_cast<uint32_t>(data[6]) << 8) | data[5];
    weight = static_cast<float>(raw) / decimalDivisor(data[9]);
    if ((data[10] & 0x02) != 0) {
        weight = -weight;
    }
    return validWeight(weight);
}

bool AcaiaArduinoBLE::parseAcaiaOldPacket(const byte data[], int length,
                                           float& weight) const {
    if ((length != 10 && length != 14) || data[6] < 1 || data[6] > 4) {
        return false;
    }
    if (length == 14 && !validAcaiaChecksum(data, length)) {
        return false;
    }

    const uint32_t raw =
        (static_cast<uint32_t>(data[3]) << 8) | data[2];
    weight = static_cast<float>(raw) / decimalDivisor(data[6]);
    if ((data[7] & 0x02) != 0) {
        weight = -weight;
    }
    return validWeight(weight);
}

bool AcaiaArduinoBLE::parseGenericPacket(const byte data[], int length,
                                          float& weight) const {
    if (length != 20 || data[0] != 0x03 ||
        (data[6] != '-' && data[6] != '+' && data[6] != ' ' &&
         data[6] != 0x00)) {
        return false;
    }

    const uint32_t raw = (static_cast<uint32_t>(data[7]) << 16) |
                         (static_cast<uint32_t>(data[8]) << 8) |
                         data[9];
    weight = static_cast<float>(raw) / 100.0f;
    if (data[6] == '-') {
        weight = -weight;
    }
    return validWeight(weight);
}

bool AcaiaArduinoBLE::parseFelicitaPacket(const byte data[], int length,
                                           float& weight) const {
    if (length != 18 ||
        (data[2] != '-' && data[2] != '+' && data[2] != ' ' &&
         data[2] != 0x00)) {
        return false;
    }
    for (int i = 3; i <= 8; ++i) {
        if (data[i] < '0' || data[i] > '9') {
            return false;
        }
    }

    const uint32_t hundredths =
        static_cast<uint32_t>(data[3] - '0') * 100000UL +
        static_cast<uint32_t>(data[4] - '0') * 10000UL +
        static_cast<uint32_t>(data[5] - '0') * 1000UL +
        static_cast<uint32_t>(data[6] - '0') * 100UL +
        static_cast<uint32_t>(data[7] - '0') * 10UL +
        static_cast<uint32_t>(data[8] - '0');
    weight = static_cast<float>(hundredths) / 100.0f;
    if (data[2] == '-') {
        weight = -weight;
    }
    return validWeight(weight);
}

bool AcaiaArduinoBLE::validAcaiaChecksum(const byte data[],
                                          int length) const {
    if (length < 6) {
        return false;
    }
    byte checksumEven = 0;
    byte checksumOdd = 0;
    int payloadIndex = 0;
    for (int i = 3; i < length - 2; ++i, ++payloadIndex) {
        if ((payloadIndex & 1) == 0) {
            checksumEven = static_cast<byte>(checksumEven + data[i]);
        } else {
            checksumOdd = static_cast<byte>(checksumOdd + data[i]);
        }
    }
    return checksumEven == data[length - 2] &&
           checksumOdd == data[length - 1];
}

bool AcaiaArduinoBLE::validWeight(float weight) const {
    return isfinite(weight) && fabsf(weight) <= MAX_SUPPORTED_WEIGHT_GRAMS;
}

bool AcaiaArduinoBLE::writeCommand(const byte command[], int length) {
    if (!isConnected() || !_write || command == nullptr || length <= 0) {
        return false;
    }
    if (_write.writeValue(command, length)) {
        return true;
    }
    resetConnection(true, AcaiaDisconnectReason::COMMAND_WRITE_FAILED);
    return false;
}

void AcaiaArduinoBLE::retainCharacteristic(
        BLECharacteristic& destination,
        const BLECharacteristic& source) {
    destination.~BLECharacteristic();
    new (&destination) BLECharacteristic(source);
}

void AcaiaArduinoBLE::clearCharacteristic(
        BLECharacteristic& characteristic) {
    characteristic.~BLECharacteristic();
    new (&characteristic) BLECharacteristic();
}

void AcaiaArduinoBLE::rememberPeripheral(const BLEDevice& peripheral) {
    _peripheral.~BLEDevice();
    new (&_peripheral) BLEDevice(peripheral);
    _hasPeripheral = true;
    const String addr = peripheral.address();
    const String name = peripheral.localName();
    strncpy(_address, addr.c_str(), sizeof(_address) - 1);
    _address[sizeof(_address) - 1] = '\0';
    strncpy(_localName, name.c_str(), sizeof(_localName) - 1);
    _localName[sizeof(_localName) - 1] = '\0';
}

void AcaiaArduinoBLE::clearPeripheral() {
    _peripheral.~BLEDevice();
    new (&_peripheral) BLEDevice();
    _hasPeripheral = false;
    _address[0] = '\0';
    _localName[0] = '\0';
}

void AcaiaArduinoBLE::resetConnection(bool disconnectPeer,
                                       AcaiaDisconnectReason reason) {
    _connected = false;
    if (reason != AcaiaDisconnectReason::NONE) {
        _lastDisconnectReason = reason;
    }

    if (_scanning) {
        BLE.stopScan();
        _scanning = false;
    }
    _scanStartedAt = 0;
    _scanMac[0] = '\0';

    // Release retained remote attributes before ArduinoBLE removes the peer's
    // service tree. This ordering also makes repeated cleanup idempotent.
    clearCharacteristic(_read);
    clearCharacteristic(_write);

    if (_hasPeripheral && disconnectPeer) {
        _peripheral.disconnect();
    }
    clearPeripheral();

    _connectedAt = 0;
    _lastPacket = 0;
    _lastHeartBeat = 0;
    _packetPeriod = 0;
    _hasValidPacket = false;
    _consecutiveRejectedPackets = 0;
    _hasTimer = false;
    _currentTimerMs = 0;
    _lastTimerPacket = 0;
    _type = OLD;
}

void AcaiaArduinoBLE::rejectPacket(const char* reason) {
    ++_rejectedPackets;
    ++_consecutiveRejectedPackets;
    if (_debug) {
        Serial.print("Rejected scale packet: ");
        Serial.println(reason);
    }
    if (_connected &&
        _consecutiveRejectedPackets >= MAX_CONSECUTIVE_REJECTED_PACKETS) {
        Serial.println(
            "Invalid scale packet stream; disconnecting (worker will rescan)");
        resetConnection(true,
                        AcaiaDisconnectReason::INVALID_PACKET_STREAM);
    }
}

AcaiaDisconnectReason AcaiaArduinoBLE::lastDisconnectReason() const {
    return _lastDisconnectReason;
}

const char* AcaiaArduinoBLE::lastDisconnectReasonName() const {
    switch (_lastDisconnectReason) {
        case AcaiaDisconnectReason::NONE: return "none";
        case AcaiaDisconnectReason::USER_REQUEST: return "user request";
        case AcaiaDisconnectReason::SCAN_START_FAILED: return "scan start failed";
        case AcaiaDisconnectReason::SCAN_TIMEOUT: return "scan timeout";
        case AcaiaDisconnectReason::CONNECT_FAILED: return "connect failed";
        case AcaiaDisconnectReason::DISCOVERY_FAILED: return "discovery failed";
        case AcaiaDisconnectReason::UNSUPPORTED_SCALE: return "unsupported scale";
        case AcaiaDisconnectReason::SUBSCRIBE_FAILED: return "subscribe failed";
        case AcaiaDisconnectReason::INITIALIZATION_WRITE_FAILED:
            return "initialization write failed";
        case AcaiaDisconnectReason::REMOTE_DISCONNECTED:
            return "remote disconnected";
        case AcaiaDisconnectReason::FIRST_PACKET_TIMEOUT:
            return "first packet timeout";
        case AcaiaDisconnectReason::PACKET_TIMEOUT: return "packet timeout";
        case AcaiaDisconnectReason::INVALID_PACKET_STREAM:
            return "invalid packet stream";
        case AcaiaDisconnectReason::COMMAND_WRITE_FAILED:
            return "command write failed";
    }
    return "unknown";
}

uint32_t AcaiaArduinoBLE::lastValidPacketAgeMs() const {
    return _hasValidPacket ? elapsedSince(_lastPacket) : 0xffffffffUL;
}

uint32_t AcaiaArduinoBLE::rejectedPacketCount() const {
    return _rejectedPackets;
}

uint32_t AcaiaArduinoBLE::reconnectCount() const {
    return _reconnects;
}

bool AcaiaArduinoBLE::isScaleName(const char *name) const {
    if (name == nullptr) {
        return false;
    }
    return strncmp(name, "CINCO", 5) == 0 || strncmp(name, "ACAIA", 5) == 0 ||
           strncmp(name, "PYXIS", 5) == 0 || strncmp(name, "LUNAR", 5) == 0 ||
           strncmp(name, "PEARL", 5) == 0 || strncmp(name, "PROCH", 5) == 0 ||
           strncmp(name, "BOOKO", 5) == 0 || strncmp(name, "FELIC", 5) == 0;
}

void AcaiaArduinoBLE::exploreService(BLEService service) {
    Serial.print("Service ");
    Serial.println(service.uuid());
    for (int i = 0; i < service.characteristicCount(); ++i) {
        exploreCharacteristic(service.characteristic(i));
    }
}

void AcaiaArduinoBLE::exploreCharacteristic(
        BLECharacteristic characteristic) {
    Serial.print("\tCharacteristic ");
    Serial.print(characteristic.uuid());
    Serial.print(", properties 0x");
    Serial.print(characteristic.properties(), HEX);

    if (characteristic.canRead()) {
        characteristic.read();
        if (characteristic.valueLength() > 0) {
            Serial.print(", value 0x");
            printData(characteristic.value(), characteristic.valueLength());
        }
    }
    Serial.println();

    for (int i = 0; i < characteristic.descriptorCount(); ++i) {
        exploreDescriptor(characteristic.descriptor(i));
    }
}

void AcaiaArduinoBLE::exploreDescriptor(BLEDescriptor descriptor) {
    Serial.print("\t\tDescriptor ");
    Serial.print(descriptor.uuid());
    descriptor.read();
    Serial.print(", value 0x");
    printData(descriptor.value(), descriptor.valueLength());
    Serial.println();
}

void AcaiaArduinoBLE::printData(const unsigned char data[], int length) {
    if (data == nullptr || length <= 0) {
        return;
    }
    for (int i = 0; i < length; ++i) {
        const unsigned char value = data[i];
        if (value < 16) {
            Serial.print('0');
        }
        Serial.print(value, HEX);
    }
}
