#include "EspressoScaleBLE.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

uint32_t fakeMillis = 0;
FakeSerialClass Serial;
BLEClass BLE;

namespace FakeBLE {
unsigned long currentTimeoutMs = 0;
}

namespace {

int checks = 0;

static_assert(!std::is_copy_constructible<EspressoScaleBLE>::value,
              "EspressoScaleBLE must remain a single owner");
static_assert(!std::is_copy_assignable<EspressoScaleBLE>::value,
              "EspressoScaleBLE must not use BLECharacteristic assignment");
static_assert(BLE_CONNECT_TIMEOUT_MS + BLE_OPERATION_TIMEOUT_MS < 5000UL,
              "GAP connect plus one ATT wait must remain under a 5 s TWDT");
static_assert(BLE_DISCOVER_TIMEOUT_MS < 5000UL,
              "GATT discovery must remain under a 5 s TWDT");

#define CHECK(condition)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(condition)) {                                                    \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__    \
                      << ": " #condition << std::endl;                        \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

bool pollUntilConnected(EspressoScaleBLE &scale, int maxSteps = 40) {
    bool connected = false;
    for (int i = 0; i < maxSteps && !connected; ++i) {
        fakeMillis += 20;
        connected = scale.pollScan();
    }
    return connected;
}

struct ScaleFixture {
    std::shared_ptr<FakeBLE::PeripheralState> peripheral;
    std::shared_ptr<FakeBLE::CharacteristicState> write;
    std::shared_ptr<FakeBLE::CharacteristicState> read;
};

ScaleFixture makeScale(scale_type type) {
    ScaleFixture fixture;
    fixture.peripheral = std::make_shared<FakeBLE::PeripheralState>();
    fixture.write = std::make_shared<FakeBLE::CharacteristicState>();
    fixture.read = std::make_shared<FakeBLE::CharacteristicState>();
    fixture.write->writable = true;
    fixture.read->writable = false;
    fixture.read->subscribable = true;

    const char* writeUuid = nullptr;
    const char* readUuid = nullptr;
    switch (type) {
        case OLD:
            fixture.peripheral->localName = "ACAIA-LUNAR";
            writeUuid = WRITE_CHAR_OLD_VERSION;
            readUuid = READ_CHAR_OLD_VERSION;
            // Old Acaia uses the same characteristic in both directions.
            fixture.read = fixture.write;
            fixture.read->subscribable = true;
            break;
        case NEW:
            fixture.peripheral->localName = "PYXIS";
            writeUuid = WRITE_CHAR_NEW_VERSION;
            readUuid = READ_CHAR_NEW_VERSION;
            break;
        case GENERIC:
            fixture.peripheral->localName = "BOOKOO";
            writeUuid = WRITE_CHAR_GENERIC;
            readUuid = READ_CHAR_GENERIC;
            break;
        case FELICITA:
            fixture.peripheral->localName = "FELICITA";
            writeUuid = WRITE_CHAR_FELICITA;
            readUuid = READ_CHAR_FELICITA;
            fixture.read = fixture.write;
            fixture.read->subscribable = true;
            break;
        case ECLAIR:
            fixture.peripheral->localName = "ECLAIR";
            writeUuid = WRITE_CHAR_ECLAIR;
            readUuid = READ_CHAR_ECLAIR;
            break;
    }

    fixture.write->uuid = writeUuid;
    fixture.read->uuid = readUuid;
    fixture.peripheral->characteristics[writeUuid] = fixture.write;
    fixture.peripheral->characteristics[readUuid] = fixture.read;
    BLE.setAvailable(fixture.peripheral);
    return fixture;
}

ScaleFixture makeNamedScale(const char *name, const char *writeUuid,
                            const char *readUuid) {
    ScaleFixture fixture;
    fixture.peripheral = std::make_shared<FakeBLE::PeripheralState>();
    fixture.write = std::make_shared<FakeBLE::CharacteristicState>();
    fixture.read = std::make_shared<FakeBLE::CharacteristicState>();
    fixture.write->writable = true;
    fixture.read->writable = false;
    fixture.read->subscribable = true;
    fixture.peripheral->localName = name == nullptr ? "" : name;
    if (std::strcmp(writeUuid, readUuid) == 0) {
        fixture.read = fixture.write;
        fixture.read->subscribable = true;
    }
    fixture.write->uuid = writeUuid;
    fixture.read->uuid = readUuid;
    fixture.peripheral->characteristics[writeUuid] = fixture.write;
    fixture.peripheral->characteristics[readUuid] = fixture.read;
    BLE.setAvailable(fixture.peripheral);
    return fixture;
}

const ScaleProtocol *protocolById(const char *id) {
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        const ScaleProtocol *protocol = scaleProtocolAt(i);
        if (std::strcmp(protocol->id, id) == 0) {
            return protocol;
        }
    }
    return nullptr;
}

std::vector<byte> encodeOp(const ScaleProtocol *protocol, ScaleOp op,
                           uint8_t arg = 0) {
    byte out[SCALE_MAX_COMMAND_LENGTH] = {};
    int length = 0;
    CHECK(protocol != nullptr);
    CHECK(protocol->encodeCommand(op, arg, out, &length));
    CHECK(length > 0);
    return std::vector<byte>(out, out + length);
}

void resetFake() {
    fakeMillis = 0;
    BLE.reset();
}

void setAcaiaChecksum(std::vector<byte>& packet) {
    byte even = 0;
    byte odd = 0;
    int payloadIndex = 0;
    for (size_t i = 3; i + 2 < packet.size(); ++i, ++payloadIndex) {
        if ((payloadIndex & 1) == 0) {
            even = static_cast<byte>(even + packet[i]);
        } else {
            odd = static_cast<byte>(odd + packet[i]);
        }
    }
    packet[packet.size() - 2] = even;
    packet[packet.size() - 1] = odd;
}

std::vector<byte> acaiaNewWeight(float grams, int length = 13) {
    std::vector<byte> packet(static_cast<size_t>(length), 0);
    packet[0] = 0xef;
    packet[1] = 0xdd;
    packet[2] = 0x0c;
    packet[3] = static_cast<byte>(length - 5);
    packet[4] = 0x05;
    const bool negative = grams < 0.0f;
    const uint16_t raw = static_cast<uint16_t>(
        std::lround(std::fabs(grams) * 10.0f));
    packet[5] = static_cast<byte>(raw & 0xff);
    packet[6] = static_cast<byte>((raw >> 8) & 0xff);
    packet[9] = 1;
    packet[10] = negative ? 0x02 : 0x00;
    setAcaiaChecksum(packet);
    return packet;
}

void notify(const ScaleFixture& fixture, const std::vector<byte>& packet) {
    fixture.read->value = packet;
    fixture.read->forcedReadLength = -1;
    fixture.read->updated = true;
}

std::vector<byte> eclairPacket(int32_t milligrams, uint32_t timerMs) {
    std::vector<byte> packet(10, 0);
    packet[0] = 'W';
    const uint32_t rawWeight = static_cast<uint32_t>(milligrams);
    for (int i = 0; i < 4; ++i) {
        packet[1 + i] = static_cast<byte>(rawWeight >> (i * 8));
        packet[5 + i] = static_cast<byte>(timerMs >> (i * 8));
    }
    byte checksum = 0;
    for (int i = 1; i <= 8; ++i) {
        checksum ^= packet[i];
    }
    packet[9] = checksum;
    return packet;
}

void testScanDiagnostics() {
    resetFake();
    BLE.scanResult = false;
    EspressoScaleBLE scale(false);
    CHECK(!scale.init());
    CHECK(BLE.timeoutMs == BLE_OPERATION_TIMEOUT_MS);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::SCAN_START_FAILED);

    resetFake();
    EspressoScaleBLE timeoutScale(false);
    CHECK(!timeoutScale.init());
    CHECK(fakeMillis >= SCALE_SCAN_TIMEOUT_MS);
    CHECK(timeoutScale.lastDisconnectReason() ==
          ScaleDisconnectReason::SCAN_TIMEOUT);
    CHECK(BLE.stopScanCalls == 1);
}

void testNonBlockingScanDoesNotRestartOrResetIdle() {
    resetFake();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 1);
    CHECK(BLE.lastWithDuplicates);
    CHECK(BLE.lastScanInterval == BLE_SCAN_IDLE_INTERVAL);
    CHECK(BLE.lastScanWindow == BLE_SCAN_IDLE_WINDOW);
    CHECK(BLE.stopScanCalls == 0);

    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 1);

    CHECK(!scale.pollScan());
    CHECK(scale.isScanning());
    fakeMillis = SCALE_SCAN_TIMEOUT_MS;
    CHECK(!scale.pollScan());
    CHECK(scale.isScanning());
    CHECK(BLE.stopScanCalls == 0);

    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 1);
    CHECK(BLE.stopScanCalls == 0);

    CHECK(scale.startScan(nullptr, true));
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 2);
    CHECK(BLE.stopScanCalls == 1);
}

void testBurstScanParametersAndDiscoverTimeoutRestored() {
    resetFake();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan(nullptr, false, true));
    CHECK(BLE.lastScanInterval == BLE_SCAN_BURST_INTERVAL);
    CHECK(BLE.lastScanWindow == BLE_SCAN_BURST_WINDOW);
    CHECK(BLE_SCAN_IDLE_INTERVAL == 0x00C0);
    CHECK(BLE_SCAN_IDLE_WINDOW == 0x0030);
    CHECK(BLE_SCAN_BURST_INTERVAL == 0x0060);
    CHECK(BLE_SCAN_BURST_WINDOW == 0x0030);
    CHECK(scale.startScan(nullptr, true, false));
    CHECK(BLE.lastScanInterval == BLE_SCAN_IDLE_INTERVAL);
    CHECK(BLE.lastScanWindow == BLE_SCAN_IDLE_WINDOW);

    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE connected(false);
    CHECK(connected.startScan());
    CHECK(pollUntilConnected(connected));
    CHECK(BLE.timeoutMs == BLE_OPERATION_TIMEOUT_MS);
}

void testNonBlockingScanConnectsWithoutInit() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan());
    CHECK(pollUntilConnected(scale));
    CHECK(scale.isConnected());
    CHECK(!scale.isScanning());
    CHECK(!scale.isConnecting());
    CHECK(fixture.peripheral->connected);
    CHECK(strcmp(scale.address(), "01:02:03:04:05:06") == 0);
    CHECK(strcmp(scale.localName(), "PYXIS") == 0);
}

void testConnectFilterUsesNameScan() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->address = "AA:BB:CC:DD:EE:FF";
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(scale.isDirectedScan());
    CHECK(BLE.scanCalls == 1);
    CHECK(BLE.scanForAddressCalls == 0);
    CHECK(BLE.lastWithDuplicates);
    CHECK(pollUntilConnected(scale));
    CHECK(scale.isConnected());
    CHECK(!scale.isDirectedScan());
    CHECK(strcmp(scale.address(), "AA:BB:CC:DD:EE:FF") == 0);

    resetFake();
    fixture = makeScale(NEW);
    fixture.peripheral->address = "11:22:33:44:55:66";
    fixture.peripheral->localName = "PYXIS";
    EspressoScaleBLE mismatch(false);
    CHECK(mismatch.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(!mismatch.pollScan());
    CHECK(mismatch.isScanning());
    char seenMac[ACAIA_MAC_CAPACITY] = {};
    char seenName[ACAIA_NAME_CAPACITY] = {};
    CHECK(mismatch.takeSeenAdvertisement(seenMac, sizeof(seenMac), seenName,
                                         sizeof(seenName)));
    CHECK(strcmp(seenMac, "11:22:33:44:55:66") == 0);
    CHECK(strcmp(seenName, "PYXIS") == 0);
    CHECK(!mismatch.takeSeenAdvertisement(seenMac, sizeof(seenMac), seenName,
                                          sizeof(seenName)));
    fakeMillis = SCALE_SCAN_TIMEOUT_MS;
    CHECK(!mismatch.pollScan());
    CHECK(mismatch.isScanning());
    CHECK(BLE.stopScanCalls == 0);
}

void testConnectFilterConnectsWithoutLocalName() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->address = "aa:bb:cc:dd:ee:ff";
    fixture.peripheral->localName.clear();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(pollUntilConnected(scale));
    CHECK(scale.isConnected());
    CHECK(strcmp(scale.address(), "aa:bb:cc:dd:ee:ff") == 0);
}

void testNameScanIgnoresEmptyLocalName() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->localName.clear();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan());
    CHECK(!scale.pollScan());
    CHECK(scale.isScanning());
    CHECK(!scale.isConnected());
}

void testStartScanRestartsOnFilterChange() {
    resetFake();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(BLE.scanCalls == 1);
    CHECK(BLE.scanForAddressCalls == 0);
    CHECK(scale.startScan(nullptr));
    CHECK(scale.isScanning());
    CHECK(!scale.isDirectedScan());
    CHECK(BLE.stopScanCalls == 1);
    CHECK(BLE.scanCalls == 2);
    CHECK(BLE.scanForAddressCalls == 0);
}

void testCleanupOnInitializationFailures() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->connectResult = false;
    EspressoScaleBLE connectFailure(false);
    CHECK(!connectFailure.init());
    CHECK(connectFailure.lastDisconnectReason() ==
          ScaleDisconnectReason::CONNECT_FAILED);
    CHECK(fixture.peripheral->connectCalls == SCALE_CONNECT_ATTEMPTS);
    CHECK(fixture.peripheral->disconnectCalls == 1);

    resetFake();
    fixture = makeScale(NEW);
    fixture.peripheral->discoveryResult = false;
    EspressoScaleBLE scale(false);
    CHECK(!scale.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::DISCOVERY_FAILED);

    resetFake();
    fixture = makeScale(NEW);
    fixture.read->subscribeResult = false;
    EspressoScaleBLE subscribeFailure(false);
    CHECK(!subscribeFailure.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(subscribeFailure.lastDisconnectReason() ==
          ScaleDisconnectReason::SUBSCRIBE_FAILED);

    resetFake();
    fixture = makeScale(NEW);
    fixture.write->writeResult = false;
    EspressoScaleBLE writeFailure(false);
    CHECK(!writeFailure.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(writeFailure.lastDisconnectReason() ==
          ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED);
}

void testConnectRetriesThenSucceeds() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->connectFailRemaining = SCALE_CONNECT_ATTEMPTS - 1;
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan());
    CHECK(pollUntilConnected(scale, 40));
    CHECK(scale.isConnected());
    CHECK(scale.isLinkUp());
    CHECK(fixture.peripheral->connectCalls == SCALE_CONNECT_ATTEMPTS);
    CHECK(fixture.peripheral->timeoutMsAtConnect == BLE_CONNECT_TIMEOUT_MS);
    CHECK(BLE.timeoutMs == BLE_OPERATION_TIMEOUT_MS);
}

void testConnectFailedOnlyAfterRetries() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->connectResult = false;
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan());
    CHECK(!pollUntilConnected(scale, 40));
    CHECK(!scale.isConnected());
    CHECK(!scale.isConnecting());
    CHECK(fixture.peripheral->connectCalls == SCALE_CONNECT_ATTEMPTS);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::CONNECT_FAILED);
    CHECK(BLE.timeoutMs == BLE_OPERATION_TIMEOUT_MS);
    CHECK(fixture.peripheral->disconnectCalls == 1);
}

void testFirstPacketAndSteadyStateTimeouts() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    uint32_t connectedAt = fakeMillis;
    fakeMillis = connectedAt + FIRST_PACKET_TIMEOUT_MS - 1;
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.isConnected());
    fakeMillis = connectedAt + FIRST_PACKET_TIMEOUT_MS;
    CHECK(!scale.newWeightAvailable());
    CHECK(!scale.isConnected());
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::FIRST_PACKET_TIMEOUT);
    CHECK(fixture.peripheral->disconnectCalls == 1);

    resetFake();
    fixture = makeScale(NEW);
    EspressoScaleBLE steadyScale(false);
    CHECK(steadyScale.init());
    notify(fixture, acaiaNewWeight(123.4f));
    CHECK(steadyScale.newWeightAvailable());
    CHECK(std::fabs(steadyScale.getWeight() - 123.4f) < 0.01f);
    CHECK(steadyScale.lastValidPacketAgeMs() == 0);
    const uint32_t lastPacketAt = fakeMillis;
    fakeMillis = lastPacketAt + MAX_PACKET_PERIOD_MS - 1;
    CHECK(!steadyScale.newWeightAvailable());
    CHECK(steadyScale.isConnected());
    fakeMillis = lastPacketAt + MAX_PACKET_PERIOD_MS;
    CHECK(!steadyScale.newWeightAvailable());
    CHECK(steadyScale.lastDisconnectReason() ==
          ScaleDisconnectReason::PACKET_TIMEOUT);

    resetFake();
    ScaleFixture genericFixture = makeScale(GENERIC);
    EspressoScaleBLE genericScale(false);
    CHECK(genericScale.init());
    std::vector<byte> genericPacket(20, 0);
    genericPacket[0] = 0x03;
    genericPacket[2] = 0x00;
    genericPacket[3] = 0x30;
    genericPacket[4] = 0x39;
    genericPacket[6] = '-';
    genericPacket[8] = 0x04;
    genericPacket[9] = 0xd2;
    notify(genericFixture, genericPacket);
    CHECK(genericScale.newWeightAvailable());
    const uint32_t genericPacketAt = fakeMillis;
    fakeMillis = genericPacketAt + GENERIC_MAX_PACKET_PERIOD_MS - 1;
    CHECK(!genericScale.newWeightAvailable());
    CHECK(genericScale.isConnected());
    fakeMillis = genericPacketAt + GENERIC_MAX_PACKET_PERIOD_MS;
    CHECK(!genericScale.newWeightAvailable());
    CHECK(genericScale.lastDisconnectReason() ==
          ScaleDisconnectReason::PACKET_TIMEOUT);
}

void testAcaiaValidationAndDebugBounds() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE scale(true);
    CHECK(scale.init());

    // A 17-byte packet used to print 17 bytes from a 13-byte stack buffer.
    notify(fixture, acaiaNewWeight(-42.5f, 17));
    CHECK(scale.newWeightAvailable());
    CHECK(std::fabs(scale.getWeight() + 42.5f) < 0.01f);

    std::vector<byte> corrupt = acaiaNewWeight(50.0f);
    corrupt.back() ^= 0x01;
    notify(fixture, corrupt);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 1);
    CHECK(std::fabs(scale.getWeight() + 42.5f) < 0.01f);

    std::vector<byte> invalidExponent = acaiaNewWeight(50.0f);
    invalidExponent[9] = 12;
    setAcaiaChecksum(invalidExponent);
    notify(fixture, invalidExponent);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 2);

    notify(fixture, acaiaNewWeight(50.0f));
    fixture.read->forcedReadLength = 12;
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 3);

    notify(fixture, std::vector<byte>(64, 0xaa));
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 4);
}

void testFelicitaAsciiValidation() {
    resetFake();
    ScaleFixture fixture = makeScale(FELICITA);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());

    std::vector<byte> packet(18, 0);
    packet[2] = '-';
    const char digits[] = "001234";
    for (int i = 0; i < 6; ++i) {
        packet[3 + i] = static_cast<byte>(digits[i]);
    }
    notify(fixture, packet);
    CHECK(scale.newWeightAvailable());
    CHECK(std::fabs(scale.getWeight() + 12.34f) < 0.01f);

    packet[5] = 'x';
    notify(fixture, packet);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 1);
}

void testEclairProtocol() {
    resetFake();
    ScaleFixture fixture = makeScale(ECLAIR);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    CHECK(std::strcmp(scale.connectedProtocolName(), "atomheart_eclair") == 0);
    CHECK(fixture.write->writes.empty());
    CHECK(!scale.heartbeatRequired());
    CHECK(!scale.supportsTareStartTimer());
    CHECK(!scale.supportsIndependentBeep());
    CHECK(!scale.supportsCommandFeedback());
    CHECK(!scaleCommandOk(scale.tareStartTimer()));
    CHECK(!scaleCommandOk(scale.beepWithoutStateChange()));
    CHECK(!scaleCommandOk(scale.setBeepLevel(0)));

    CHECK(scaleCommandOk(scale.tare()));
    CHECK(scaleCommandOk(scale.startTimer()));
    CHECK(scaleCommandOk(scale.stopTimer()));
    CHECK(scaleCommandOk(scale.resetTimer()));
    CHECK(fixture.write->writes.size() == 4);
    CHECK(fixture.write->writes[0] == std::vector<byte>({0x54, 0x01, 0x01}));
    CHECK(fixture.write->writes[1] == std::vector<byte>({0x53, 0x01, 0x01}));
    CHECK(fixture.write->writes[2] == std::vector<byte>({0x45, 0x01, 0x01}));
    CHECK(fixture.write->writes[3] == std::vector<byte>({0x52, 0x01, 0x01}));

    notify(fixture, eclairPacket(-12345, 65432));
    CHECK(scale.newWeightAvailable());
    CHECK(std::fabs(scale.getWeight() + 12.345f) < 0.001f);
    CHECK(scale.hasTimer());
    CHECK(scale.getTimerMs() == 65432UL);

    std::vector<byte> invalidChecksum = eclairPacket(1000, 100);
    invalidChecksum[9] ^= 0xff;
    notify(fixture, invalidChecksum);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 1);

    std::vector<byte> invalidHeader = eclairPacket(1000, 100);
    invalidHeader[0] = 'X';
    notify(fixture, invalidHeader);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 2);

    notify(fixture, eclairPacket(10000001, 100));
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.rejectedPacketCount() == 3);
}

void testDirectedEclairDiscoveryWithoutName() {
    resetFake();
    ScaleFixture fixture = makeScale(ECLAIR);
    fixture.peripheral->address = "aa:bb:cc:dd:ee:ff";
    fixture.peripheral->localName.clear();
    EspressoScaleBLE scale(false);
    CHECK(scale.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(pollUntilConnected(scale));
    CHECK(scale.isConnected());
    CHECK(std::strcmp(scale.connectedProtocolName(), "atomheart_eclair") == 0);
}

void testCapabilitiesAndWriteCleanup() {
    resetFake();
    ScaleFixture acaia = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    CHECK(scale.heartbeatRequired());
    const size_t initializationWrites = acaia.write->writes.size();
    CHECK(!scaleCommandOk(scale.tareStartTimer()));
    CHECK(!scaleCommandOk(scale.beep()));
    CHECK(!scaleCommandOk(scale.setBeepLevel(0)));
    CHECK(acaia.write->writes.size() == initializationWrites);

    acaia.write->writeResult = false;
    CHECK(!scaleCommandOk(scale.tare()));
    CHECK(!scale.isConnected());
    CHECK(!acaia.peripheral->connected);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::COMMAND_WRITE_FAILED);

    resetFake();
    ScaleFixture generic = makeScale(GENERIC);
    EspressoScaleBLE genericScale(false);
    CHECK(genericScale.init());
    CHECK(genericScale.supportsTareStartTimer());
    CHECK(scaleCommandOk(genericScale.tareStartTimer()));
    CHECK(genericScale.supportsIndependentBeep());
    CHECK(scaleCommandOk(genericScale.beep()));
    CHECK(generic.write->writes.size() == 3);
    CHECK(generic.write->writes[0] ==
          (std::vector<byte>{0x03, 0x0a, 0x08, 0x00, 0x00, 0x01}));
    CHECK(generic.write->writes[1][2] == 0x07);
    CHECK(generic.write->writes[2][2] == 0x02);
    CHECK(generic.write->writes[2][4] == 0x01);
    CHECK(generic.write->writes[2][5] == 0x0a);

    const size_t beforeLevels = generic.write->writes.size();
    for (uint8_t level = 0; level <= 5; ++level) {
        CHECK(scaleCommandOk(genericScale.setBeepLevel(level)));
        const std::vector<byte> &packet =
            generic.write->writes[beforeLevels + level];
        CHECK(packet.size() == 6);
        CHECK(packet[0] == 0x03);
        CHECK(packet[1] == 0x0a);
        CHECK(packet[2] == 0x02);
        CHECK(packet[3] == 0x00);
        CHECK(packet[4] == level);
        CHECK(packet[5] == static_cast<byte>(0x03 ^ 0x0a ^ 0x02 ^ 0x00 ^ level));
    }
    CHECK(scaleCommandOk(genericScale.beepWithoutStateChange()));
    const std::vector<byte> &beepPacket = generic.write->writes.back();
    CHECK(beepPacket == generic.write->writes[beforeLevels + 1]);
    const size_t afterValid = generic.write->writes.size();
    CHECK(!scaleCommandOk(genericScale.setBeepLevel(6)));
    CHECK(generic.write->writes.size() == afterValid);
}

void testOldAndGenericPacketValidation() {
    resetFake();
    ScaleFixture old = makeScale(OLD);
    EspressoScaleBLE oldScale(false);
    CHECK(oldScale.init());
    std::vector<byte> oldPacket(10, 0);
    oldPacket[2] = 0xd2;
    oldPacket[3] = 0x04;
    oldPacket[6] = 1;
    notify(old, oldPacket);
    CHECK(oldScale.newWeightAvailable());
    CHECK(std::fabs(oldScale.getWeight() - 123.4f) < 0.01f);
    CHECK(!oldScale.hasTimer());
    oldPacket[6] = 5;
    notify(old, oldPacket);
    CHECK(!oldScale.newWeightAvailable());

    std::vector<byte> old14(14, 0);
    old14[2] = 0xd2;
    old14[3] = 0x04;
    old14[6] = 1;
    setAcaiaChecksum(old14);
    notify(old, old14);
    CHECK(oldScale.newWeightAvailable());
    CHECK(std::fabs(oldScale.getWeight() - 123.4f) < 0.01f);
    old14[old14.size() - 1] ^= 0xff;
    notify(old, old14);
    CHECK(!oldScale.newWeightAvailable());

    resetFake();
    ScaleFixture generic = makeScale(GENERIC);
    EspressoScaleBLE genericScale(false);
    CHECK(genericScale.init());
    std::vector<byte> genericPacket(20, 0);
    genericPacket[0] = 0x03;
    genericPacket[2] = 0x00;
    genericPacket[3] = 0x30;
    genericPacket[4] = 0x39;
    genericPacket[6] = '-';
    genericPacket[8] = 0x04;
    genericPacket[9] = 0xd2;
    notify(generic, genericPacket);
    CHECK(genericScale.newWeightAvailable());
    CHECK(std::fabs(genericScale.getWeight() + 12.34f) < 0.01f);
    CHECK(genericScale.hasTimer());
    CHECK(genericScale.getTimerMs() == 12345UL);
    genericPacket[0] = 0xff;
    notify(generic, genericPacket);
    CHECK(!genericScale.newWeightAvailable());
}

std::vector<byte> acaiaNewTimer(byte minutes, byte seconds, byte tenths,
                                int length = 10) {
    std::vector<byte> packet(static_cast<size_t>(length), 0);
    packet[0] = 0xef;
    packet[1] = 0xdd;
    packet[2] = 0x0c;
    packet[3] = static_cast<byte>(length - 5);
    packet[4] = 0x07;
    packet[5] = minutes;
    packet[6] = seconds;
    packet[7] = tenths;
    setAcaiaChecksum(packet);
    return packet;
}

void testScaleTimerParsing() {
    resetFake();
    ScaleFixture acaia = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    notify(acaia, acaiaNewWeight(18.5f));
    CHECK(scale.newWeightAvailable());
    CHECK(!scale.hasTimer());

    notify(acaia, acaiaNewTimer(1, 5, 3));
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.hasTimer());
    CHECK(scale.getTimerMs() == 65300UL);
    CHECK(std::fabs(scale.getWeight() - 18.5f) < 0.01f);

    std::vector<byte> weightWithTimer = acaiaNewWeight(20.0f, 17);
    weightWithTimer[11] = 0;
    weightWithTimer[12] = 12;
    weightWithTimer[13] = 4;
    setAcaiaChecksum(weightWithTimer);
    notify(acaia, weightWithTimer);
    CHECK(scale.newWeightAvailable());
    CHECK(std::fabs(scale.getWeight() - 20.0f) < 0.01f);
    CHECK(scale.getTimerMs() == 12400UL);
    scale.disconnect();
    CHECK(!scale.hasTimer());

    resetFake();
    ScaleFixture felicita = makeScale(FELICITA);
    EspressoScaleBLE felicitaScale(false);
    CHECK(felicitaScale.init());
    std::vector<byte> packet(18, 0);
    packet[2] = '+';
    const char digits[] = "001234";
    for (int i = 0; i < 6; ++i) {
        packet[3 + i] = static_cast<byte>(digits[i]);
    }
    const char timeDigits[] = "01053";
    for (int i = 0; i < 5; ++i) {
        packet[9 + i] = static_cast<byte>(timeDigits[i]);
    }
    notify(felicita, packet);
    CHECK(felicitaScale.newWeightAvailable());
    CHECK(std::fabs(felicitaScale.getWeight() - 12.34f) < 0.01f);
    CHECK(felicitaScale.hasTimer());
    CHECK(felicitaScale.getTimerMs() == 65300UL);
}

void testRemoteDisconnectAndReconnectTelemetry() {
    resetFake();
    ScaleFixture first = makeScale(OLD);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    first.peripheral->connected = false;
    CHECK(scale.isConnected());
    fakeMillis += LINK_DOWN_DEBOUNCE_MS;
    CHECK(!scale.isConnected());
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::REMOTE_DISCONNECTED);

    ScaleFixture second = makeScale(OLD);
    CHECK(scale.init());
    CHECK(scale.reconnectCount() == 1);
    scale.disconnect();
    CHECK(!second.peripheral->connected);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::USER_REQUEST);
}

void testRejectedPacketsDoNotRefreshAvailability() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    notify(fixture, acaiaNewWeight(10.0f));
    CHECK(scale.newWeightAvailable());
    const uint32_t lastPacketAt = fakeMillis;

    fakeMillis = lastPacketAt + MAX_PACKET_PERIOD_MS - 1;
    std::vector<byte> invalid = acaiaNewWeight(11.0f);
    invalid[0] = 0;
    notify(fixture, invalid);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.isConnected());

    fakeMillis = lastPacketAt + MAX_PACKET_PERIOD_MS;
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::PACKET_TIMEOUT);
}

void testPacketLengthCorpusAndReconnectSoak() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    EspressoScaleBLE scale(false);
    CHECK(scale.init());
    for (int length = 0; length <= 64; ++length) {
        if (!scale.isConnected()) {
            fixture = makeScale(NEW);
            CHECK(scale.init());
        }
        notify(fixture,
               std::vector<byte>(static_cast<size_t>(length), 0xa5));
        CHECK(!scale.newWeightAvailable());
    }
    CHECK(scale.rejectedPacketCount() == 65);
    CHECK(scale.reconnectCount() > 0);
    CHECK(scale.lastDisconnectReason() ==
          ScaleDisconnectReason::INVALID_PACKET_STREAM);

    resetFake();
    EspressoScaleBLE reconnecting(false);
    for (int i = 0; i < 10000; ++i) {
        ScaleFixture cycle = makeScale(OLD);
        CHECK(reconnecting.init());
        cycle.peripheral->connected = false;
        CHECK(reconnecting.isConnected());
        fakeMillis += LINK_DOWN_DEBOUNCE_MS;
        CHECK(!reconnecting.isConnected());
    }
    CHECK(reconnecting.reconnectCount() == 9999);
}

void testGoldenCommandPayloadsAndFeatures() {
    CHECK(scaleProtocolCount() == 11);
    CHECK(std::strcmp(scaleProtocolAt(0)->id, "acaia_legacy") == 0);
    CHECK(std::strcmp(scaleProtocolAt(1)->id, "acaia") == 0);
    CHECK(std::strcmp(scaleProtocolAt(2)->id, "bookoo_generic") == 0);
    CHECK(std::strcmp(scaleProtocolAt(3)->id, "felicita") == 0);
    CHECK(std::strcmp(scaleProtocolAt(4)->id, "atomheart_eclair") == 0);
    CHECK(std::strcmp(scaleProtocolAt(5)->id, "decent") == 0);
    CHECK(std::strcmp(scaleProtocolAt(6)->id, "difluid") == 0);
    CHECK(std::strcmp(scaleProtocolAt(7)->id, "myscale") == 0);
    CHECK(std::strcmp(scaleProtocolAt(8)->id, "weighmybru") == 0);
    CHECK(std::strcmp(scaleProtocolAt(9)->id, "varia") == 0);
    CHECK(std::strcmp(scaleProtocolAt(10)->id, "eureka") == 0);
    CHECK(!scaleProtocolAt(0)->requireAdvertisedName);
    CHECK(scaleProtocolAt(9)->requireAdvertisedName);
    CHECK(scaleProtocolAt(10)->requireAdvertisedName);
    CHECK(scaleNameIsCompatible("CINCO"));
    CHECK(scaleNameIsCompatible("ACAIA"));
    CHECK(scaleNameIsCompatible("PYXIS"));
    CHECK(scaleNameIsCompatible("LUNAR"));
    CHECK(scaleNameIsCompatible("PEARL"));
    CHECK(scaleNameIsCompatible("PROCH"));
    CHECK(scaleNameIsCompatible("BOOKOO"));
    CHECK(scaleNameIsCompatible("FELICITA"));
    CHECK(scaleNameIsCompatible("ECLAIR"));
    CHECK(scaleNameIsCompatible("Decent Scale"));
    CHECK(scaleNameIsCompatible("EspressiScale"));
    CHECK(scaleNameIsCompatible("Microbalance Ti"));
    CHECK(scaleNameIsCompatible("blackcoffee"));
    CHECK(scaleNameIsCompatible("my_scale"));
    CHECK(scaleNameIsCompatible("MY_SCALE"));
    CHECK(scaleNameIsCompatible("WeighMyBru"));
    CHECK(scaleNameIsCompatible("VARIA AKU"));
    CHECK(scaleNameIsCompatible("Varia AKU"));
    CHECK(scaleNameIsCompatible("AKU MINI SCALE"));
    CHECK(scaleNameIsCompatible("AKU SCALE"));
    CHECK(scaleNameIsCompatible("CFS-9002"));
    CHECK(scaleNameIsCompatible("LSJ-001"));
    CHECK(!scaleNameIsCompatible("PHONE"));
    CHECK(!scaleNameIsCompatible("MbSomething"));
    CHECK(!scaleNameIsCompatible("black"));
    CHECK(!scaleNameIsCompatible("Weigh"));
    CHECK(!scaleNameIsCompatible("AKU"));

    resetFake();
    ScaleFixture acaia = makeScale(NEW);
    EspressoScaleBLE acaiaScale(false);
    CHECK(acaiaScale.init());
    CHECK(acaiaScale.features().has(ScaleFeatureWeight));
    CHECK(acaiaScale.features().has(ScaleFeatureTare));
    CHECK(acaiaScale.features().has(ScaleFeatureHeartbeat));
    CHECK(acaiaScale.features().has(ScaleFeatureCommandAudibleFeedback));
    CHECK(!acaiaScale.features().has(ScaleFeatureVolume));
    CHECK(!acaiaScale.features().has(ScaleFeatureCombinedTareStart));
    CHECK(acaia.write->writes.size() == 2);
    CHECK(acaia.write->writes[0] ==
          (std::vector<byte>{0xef, 0xdd, 0x0b, 0x30, 0x31, 0x32, 0x33, 0x34,
                             0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32,
                             0x33, 0x34, 0x9a, 0x6d}));
    CHECK(acaia.write->writes[1] ==
          (std::vector<byte>{0xef, 0xdd, 0x0c, 0x09, 0x00, 0x01, 0x01, 0x02,
                             0x02, 0x05, 0x03, 0x04, 0x15, 0x06}));
    CHECK(scaleCommandOk(acaiaScale.tare()));
    CHECK(scaleCommandOk(acaiaScale.startTimer()));
    CHECK(scaleCommandOk(acaiaScale.stopTimer()));
    CHECK(scaleCommandOk(acaiaScale.resetTimer()));
    CHECK(scaleCommandOk(acaiaScale.heartbeat()));
    CHECK(acaia.write->writes[2] ==
          (std::vector<byte>{0xef, 0xdd, 0x04, 0x00, 0x00, 0x00}));
    CHECK(acaia.write->writes[3] ==
          (std::vector<byte>{0xef, 0xdd, 0x0d, 0x00, 0x00, 0x00, 0x00}));
    CHECK(acaia.write->writes[4] ==
          (std::vector<byte>{0xef, 0xdd, 0x0d, 0x00, 0x02, 0x00, 0x02}));
    CHECK(acaia.write->writes[5] ==
          (std::vector<byte>{0xef, 0xdd, 0x0d, 0x00, 0x01, 0x00, 0x01}));
    CHECK(acaia.write->writes[6] ==
          (std::vector<byte>{0xef, 0xdd, 0x00, 0x02, 0x00, 0x02, 0x00}));
    acaiaScale.disconnect();
    CHECK(acaiaScale.features().flags == 0);

    resetFake();
    ScaleFixture generic = makeScale(GENERIC);
    EspressoScaleBLE genericScale(false);
    CHECK(genericScale.init());
    CHECK(generic.write->writes.size() == 1);
    CHECK(generic.write->writes[0] ==
          (std::vector<byte>{0x03, 0x0a, 0x08, 0x00, 0x00, 0x01}));
    CHECK(genericScale.features().has(ScaleFeatureCombinedTareStart));
    CHECK(genericScale.features().has(ScaleFeatureVolume));
    CHECK(genericScale.features().volumeMax == 5);
    CHECK(genericScale.features().maxPacketSilenceMs ==
          GENERIC_MAX_PACKET_PERIOD_MS);
    CHECK(scaleCommandOk(genericScale.tare()));
    CHECK(scaleCommandOk(genericScale.startTimer()));
    CHECK(scaleCommandOk(genericScale.stopTimer()));
    CHECK(scaleCommandOk(genericScale.resetTimer()));
    CHECK(scaleCommandOk(genericScale.tareStartTimer()));
    CHECK(generic.write->writes[1] ==
          (std::vector<byte>{0x03, 0x0a, 0x01, 0x00, 0x00, 0x08}));
    CHECK(generic.write->writes[2] ==
          (std::vector<byte>{0x03, 0x0a, 0x04, 0x00, 0x00, 0x0a}));
    CHECK(generic.write->writes[3] ==
          (std::vector<byte>{0x03, 0x0a, 0x05, 0x00, 0x00, 0x0d}));
    CHECK(generic.write->writes[4] ==
          (std::vector<byte>{0x03, 0x0a, 0x06, 0x00, 0x00, 0x0c}));
    CHECK(generic.write->writes[5] ==
          (std::vector<byte>{0x03, 0x0a, 0x07, 0x00, 0x00, 0x00}));

    resetFake();
    ScaleFixture felicita = makeScale(FELICITA);
    EspressoScaleBLE felicitaScale(false);
    CHECK(felicitaScale.init());
    CHECK(felicita.write->writes.size() == 1);
    CHECK(felicita.write->writes[0] == (std::vector<byte>{0x32}));
    CHECK(felicitaScale.features().has(ScaleFeatureCommandAudibleFeedback));
    CHECK(!felicitaScale.features().has(ScaleFeatureVolume));
    CHECK(scaleCommandOk(felicitaScale.tare()));
    CHECK(scaleCommandOk(felicitaScale.startTimer()));
    CHECK(scaleCommandOk(felicitaScale.stopTimer()));
    CHECK(scaleCommandOk(felicitaScale.resetTimer()));
    CHECK(felicita.write->writes[1] == (std::vector<byte>{0x54}));
    CHECK(felicita.write->writes[2] == (std::vector<byte>{0x52}));
    CHECK(felicita.write->writes[3] == (std::vector<byte>{0x53}));
    CHECK(felicita.write->writes[4] == (std::vector<byte>{0x43}));

    resetFake();
    ScaleFixture eclair = makeScale(ECLAIR);
    EspressoScaleBLE eclairScale(false);
    CHECK(eclairScale.init());
    CHECK(eclair.write->writes.empty());
    CHECK(!eclairScale.features().has(ScaleFeatureCommandAudibleFeedback));
    CHECK(!eclairScale.features().has(ScaleFeatureHeartbeat));
}

void testGaggimateScaleProtocols() {
    const ScaleProtocol *decent = protocolById("decent");
    CHECK(encodeOp(decent, ScaleOp::Tare) ==
          (std::vector<byte>{0x03, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x0d}));
    CHECK(encodeOp(decent, ScaleOp::Heartbeat) ==
          (std::vector<byte>{0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a}));
    {
        byte out[SCALE_MAX_COMMAND_LENGTH] = {};
        int length = 0;
        CHECK(!decent->encodeCommand(ScaleOp::StartTimer, 0, out, &length));
    }

    const ScaleProtocol *difluid = protocolById("difluid");
    CHECK(encodeOp(difluid, ScaleOp::Tare) ==
          (std::vector<byte>{0xdf, 0xdf, 0x03, 0x02, 0x01, 0x01, 0xc5}));
    CHECK(encodeOp(difluid, ScaleOp::Heartbeat) ==
          (std::vector<byte>{0xdf, 0xdf, 0x03, 0x05, 0x00, 0xc6}));

    const ScaleProtocol *myscale = protocolById("myscale");
    CHECK(encodeOp(myscale, ScaleOp::Tare).size() == 20);
    CHECK(encodeOp(myscale, ScaleOp::Tare)[0] == 0xac);

    const ScaleProtocol *weighmybru = protocolById("weighmybru");
    CHECK(encodeOp(weighmybru, ScaleOp::Tare) ==
          (std::vector<byte>{0x03, 0x0a, 0x01, 0x01, 0x00, 0x09}));

    const ScaleProtocol *varia = protocolById("varia");
    CHECK(encodeOp(varia, ScaleOp::Tare) ==
          (std::vector<byte>{0xfa, 0x82, 0x01, 0x01, 0x82}));
    CHECK(encodeOp(varia, ScaleOp::StartTimer) ==
          (std::vector<byte>{0xfa, 0x88, 0x01, 0x01, 0x88}));
    CHECK(encodeOp(varia, ScaleOp::StopTimer) ==
          (std::vector<byte>{0xfa, 0x89, 0x01, 0x02, 0x8a}));
    CHECK(encodeOp(varia, ScaleOp::ResetTimer) ==
          (std::vector<byte>{0xfa, 0x8a, 0x01, 0x03, 0x88}));

    const ScaleProtocol *eureka = protocolById("eureka");
    CHECK(encodeOp(eureka, ScaleOp::Tare) ==
          (std::vector<byte>{0xaa, 0x02, 0x31, 0x31, 0x00, 0x00}));
    CHECK(encodeOp(eureka, ScaleOp::StartTimer) ==
          (std::vector<byte>{0xaa, 0x02, 0x33, 0x33, 0x00, 0x00}));

    resetFake();
    ScaleFixture decentFix =
        makeNamedScale("Decent Scale", "36f5", "fff4");
    EspressoScaleBLE decentScale(false);
    CHECK(decentScale.init());
    CHECK(std::strcmp(decentScale.connectedProtocolName(), "decent") == 0);
    CHECK(decentScale.features().has(ScaleFeatureHeartbeat));
    CHECK(decentScale.features().heartbeatPeriodMs == 5000);
    CHECK(!decentScale.features().has(ScaleFeatureStartTimer));
    std::vector<byte> decentWeight{0x03, 0xce, 0x00, 0xc8, 0x00, 0x00, 0x00};
    decentWeight[6] = static_cast<byte>(
        0x03 ^ 0xce ^ 0x00 ^ 0xc8 ^ 0x00 ^ 0x00);
    notify(decentFix, decentWeight);
    CHECK(decentScale.newWeightAvailable());
    CHECK(std::fabs(decentScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture difluidFix = makeNamedScale("Microbalance", "aa01", "aa01");
    EspressoScaleBLE difluidScale(false);
    CHECK(difluidScale.init());
    CHECK(std::strcmp(difluidScale.connectedProtocolName(), "difluid") == 0);
    CHECK(difluidFix.write->writes.size() == 2);
    CHECK(difluidFix.write->writes[0] ==
          (std::vector<byte>{0xdf, 0xdf, 0x01, 0x04, 0x01, 0x00, 0xc4}));
    CHECK(difluidFix.write->writes[1] ==
          (std::vector<byte>{0xdf, 0xdf, 0x01, 0x00, 0x01, 0x01, 0xc1}));
    std::vector<byte> difluidWeight(19, 0);
    difluidWeight[0] = 0xdf;
    difluidWeight[1] = 0xdf;
    difluidWeight[2] = 0x03;
    difluidWeight[3] = 0x00;
    difluidWeight[4] = 0x0d;
    difluidWeight[7] = 0x00;
    difluidWeight[8] = 0xc8;
    uint16_t sum = 0;
    for (size_t i = 0; i + 1 < difluidWeight.size(); ++i) {
        sum = static_cast<uint16_t>(sum + difluidWeight[i]);
    }
    difluidWeight[18] = static_cast<byte>(sum & 0xff);
    notify(difluidFix, difluidWeight);
    CHECK(difluidScale.newWeightAvailable());
    CHECK(std::fabs(difluidScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture myscaleFix =
        makeNamedScale("blackcoffee", "ffb1", "ffb2");
    EspressoScaleBLE myscaleScale(false);
    CHECK(myscaleScale.init());
    CHECK(std::strcmp(myscaleScale.connectedProtocolName(), "myscale") == 0);
    std::vector<byte> myscaleWeight(15, 0);
    myscaleWeight[4] = 0x00;
    myscaleWeight[5] = 0x4e;
    myscaleWeight[6] = 0x20;
    notify(myscaleFix, myscaleWeight);
    CHECK(myscaleScale.newWeightAvailable());
    CHECK(std::fabs(myscaleScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture wmbFix = makeNamedScale(
        "WeighMyBru", "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E");
    EspressoScaleBLE wmbScale(false);
    CHECK(wmbScale.init());
    CHECK(std::strcmp(wmbScale.connectedProtocolName(), "weighmybru") == 0);
    CHECK(wmbScale.features().maxPacketSilenceMs == 8000);
    CHECK(!wmbScale.features().has(ScaleFeatureHeartbeat));
    std::vector<byte> wmbWeight(20, 0);
    wmbWeight[0] = 0x03;
    wmbWeight[1] = 0x0b;
    wmbWeight[6] = '+';
    wmbWeight[7] = 0x00;
    wmbWeight[8] = 0x07;
    wmbWeight[9] = 0xd0;
    wmbWeight[19] = 0;
    for (size_t i = 0; i < 19; ++i) {
        wmbWeight[19] ^= wmbWeight[i];
    }
    notify(wmbFix, wmbWeight);
    CHECK(wmbScale.newWeightAvailable());
    CHECK(std::fabs(wmbScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture variaFix = makeNamedScale("VARIA AKU", "fff2", "fff1");
    EspressoScaleBLE variaScale(false);
    CHECK(variaScale.init());
    CHECK(std::strcmp(variaScale.connectedProtocolName(), "varia") == 0);
    CHECK(variaScale.features().has(ScaleFeatureStartTimer));
    std::vector<byte> variaWeight{0xfa, 0x01, 0x03, 0x00, 0x07, 0xd0, 0x00};
    variaWeight[6] = static_cast<byte>(0x01 ^ 0x03 ^ 0x00 ^ 0x07 ^ 0xd0);
    notify(variaFix, variaWeight);
    CHECK(variaScale.newWeightAvailable());
    CHECK(std::fabs(variaScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture eurekaFix = makeNamedScale("CFS-9002", "fff2", "fff1");
    EspressoScaleBLE eurekaScale(false);
    CHECK(eurekaScale.init());
    CHECK(std::strcmp(eurekaScale.connectedProtocolName(), "eureka") == 0);
    std::vector<byte> eurekaWeight(11, 0);
    eurekaWeight[7] = 200;
    eurekaWeight[8] = 0;
    notify(eurekaFix, eurekaWeight);
    CHECK(eurekaScale.newWeightAvailable());
    CHECK(std::fabs(eurekaScale.getWeight() - 20.0f) < 0.01f);

    resetFake();
    ScaleFixture stolen = makeNamedScale("CFS-9002", "fff2", "fff1");
    EspressoScaleBLE stolenScale(false);
    CHECK(stolenScale.init());
    CHECK(std::strcmp(stolenScale.connectedProtocolName(), "eureka") == 0);

    resetFake();
    ScaleFixture namelessVaria = makeNamedScale("", "fff2", "fff1");
    namelessVaria.peripheral->address = "aa:bb:cc:dd:ee:ff";
    EspressoScaleBLE nameless(false);
    CHECK(nameless.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(!pollUntilConnected(nameless));
    CHECK(!nameless.isConnected());
    CHECK(nameless.lastDisconnectReason() ==
          ScaleDisconnectReason::UNSUPPORTED_SCALE);

    resetFake();
    ScaleFixture acaiaNoName = makeScale(NEW);
    acaiaNoName.peripheral->localName.clear();
    acaiaNoName.peripheral->address = "aa:bb:cc:dd:ee:ff";
    EspressoScaleBLE acaiaDirected(false);
    CHECK(acaiaDirected.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(pollUntilConnected(acaiaDirected));
    CHECK(std::strcmp(acaiaDirected.connectedProtocolName(), "acaia") == 0);
}

} // namespace

int main() {
    testScanDiagnostics();
    testNonBlockingScanDoesNotRestartOrResetIdle();
    testBurstScanParametersAndDiscoverTimeoutRestored();
    testNonBlockingScanConnectsWithoutInit();
    testConnectFilterUsesNameScan();
    testConnectFilterConnectsWithoutLocalName();
    testNameScanIgnoresEmptyLocalName();
    testStartScanRestartsOnFilterChange();
    testCleanupOnInitializationFailures();
    testConnectRetriesThenSucceeds();
    testConnectFailedOnlyAfterRetries();
    testFirstPacketAndSteadyStateTimeouts();
    testAcaiaValidationAndDebugBounds();
    testFelicitaAsciiValidation();
    testEclairProtocol();
    testDirectedEclairDiscoveryWithoutName();
    testCapabilitiesAndWriteCleanup();
    testOldAndGenericPacketValidation();
    testScaleTimerParsing();
    testRemoteDisconnectAndReconnectTelemetry();
    testRejectedPacketsDoNotRefreshAvailability();
    testPacketLengthCorpusAndReconnectSoak();
    testGoldenCommandPayloadsAndFeatures();
    testGaggimateScaleProtocols();
    std::cout << "EspressoScaleBLE host tests passed: " << checks
              << " checks" << std::endl;
    return 0;
}
