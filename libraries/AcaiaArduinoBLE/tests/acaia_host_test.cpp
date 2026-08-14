#include "AcaiaArduinoBLE.h"

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

namespace {

int checks = 0;

static_assert(!std::is_copy_constructible<AcaiaArduinoBLE>::value,
              "AcaiaArduinoBLE must remain a single owner");
static_assert(!std::is_copy_assignable<AcaiaArduinoBLE>::value,
              "AcaiaArduinoBLE must not use BLECharacteristic assignment");

#define CHECK(condition)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(condition)) {                                                    \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__    \
                      << ": " #condition << std::endl;                        \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

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
    }

    fixture.write->uuid = writeUuid;
    fixture.read->uuid = readUuid;
    fixture.peripheral->characteristics[writeUuid] = fixture.write;
    fixture.peripheral->characteristics[readUuid] = fixture.read;
    BLE.setAvailable(fixture.peripheral);
    return fixture;
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

void testScanDiagnostics() {
    resetFake();
    BLE.scanResult = false;
    AcaiaArduinoBLE scale(false);
    CHECK(!scale.init());
    CHECK(BLE.timeoutMs == BLE_OPERATION_TIMEOUT_MS);
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::SCAN_START_FAILED);

    resetFake();
    AcaiaArduinoBLE timeoutScale(false);
    CHECK(!timeoutScale.init());
    CHECK(fakeMillis >= SCALE_SCAN_TIMEOUT_MS);
    CHECK(timeoutScale.lastDisconnectReason() ==
          AcaiaDisconnectReason::SCAN_TIMEOUT);
    CHECK(BLE.stopScanCalls == 1);
}

void testNonBlockingScanDoesNotRestartOrResetIdle() {
    resetFake();
    AcaiaArduinoBLE scale(false);
    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 1);
    CHECK(BLE.stopScanCalls == 0);

    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 1);

    CHECK(!scale.pollScan());
    CHECK(scale.isScanning());
    fakeMillis = SCALE_SCAN_TIMEOUT_MS;
    CHECK(!scale.pollScan());
    CHECK(!scale.isScanning());
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::SCAN_TIMEOUT);
    CHECK(BLE.stopScanCalls == 1);

    const int stopsAfterTimeout = BLE.stopScanCalls;
    CHECK(scale.startScan());
    CHECK(scale.isScanning());
    CHECK(BLE.scanCalls == 2);
    CHECK(BLE.stopScanCalls == stopsAfterTimeout);
}

void testNonBlockingScanConnectsWithoutInit() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    AcaiaArduinoBLE scale(false);
    CHECK(scale.startScan());
    CHECK(scale.pollScan());
    CHECK(scale.isConnected());
    CHECK(!scale.isScanning());
    CHECK(fixture.peripheral->connected);
    CHECK(strcmp(scale.address(), "01:02:03:04:05:06") == 0);
    CHECK(strcmp(scale.localName(), "PYXIS") == 0);
}

void testDirectedScanUsesAddressFilter() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->address = "AA:BB:CC:DD:EE:FF";
    AcaiaArduinoBLE scale(false);
    CHECK(scale.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(scale.isDirectedScan());
    CHECK(BLE.scanCalls == 0);
    CHECK(BLE.scanForAddressCalls == 1);
    CHECK(BLE.lastScanAddress == "AA:BB:CC:DD:EE:FF");
    CHECK(scale.pollScan());
    CHECK(scale.isConnected());
    CHECK(!scale.isDirectedScan());
    CHECK(strcmp(scale.address(), "AA:BB:CC:DD:EE:FF") == 0);

    resetFake();
    fixture = makeScale(NEW);
    fixture.peripheral->address = "11:22:33:44:55:66";
    AcaiaArduinoBLE mismatch(false);
    CHECK(mismatch.startScan("AA:BB:CC:DD:EE:FF"));
    CHECK(!mismatch.pollScan());
    CHECK(mismatch.isScanning());
    fakeMillis = SCALE_SCAN_TIMEOUT_MS;
    CHECK(!mismatch.pollScan());
    CHECK(!mismatch.isScanning());
    CHECK(mismatch.lastDisconnectReason() ==
          AcaiaDisconnectReason::SCAN_TIMEOUT);
}

void testCleanupOnInitializationFailures() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    fixture.peripheral->connectResult = false;
    AcaiaArduinoBLE connectFailure(false);
    CHECK(!connectFailure.init());
    CHECK(connectFailure.lastDisconnectReason() ==
          AcaiaDisconnectReason::CONNECT_FAILED);

    resetFake();
    fixture = makeScale(NEW);
    fixture.peripheral->discoveryResult = false;
    AcaiaArduinoBLE scale(false);
    CHECK(!scale.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::DISCOVERY_FAILED);

    resetFake();
    fixture = makeScale(NEW);
    fixture.read->subscribeResult = false;
    AcaiaArduinoBLE subscribeFailure(false);
    CHECK(!subscribeFailure.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(subscribeFailure.lastDisconnectReason() ==
          AcaiaDisconnectReason::SUBSCRIBE_FAILED);

    resetFake();
    fixture = makeScale(NEW);
    fixture.write->writeResult = false;
    AcaiaArduinoBLE writeFailure(false);
    CHECK(!writeFailure.init());
    CHECK(!fixture.peripheral->connected);
    CHECK(fixture.peripheral->disconnectCalls == 1);
    CHECK(writeFailure.lastDisconnectReason() ==
          AcaiaDisconnectReason::INITIALIZATION_WRITE_FAILED);
}

void testFirstPacketAndSteadyStateTimeouts() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    AcaiaArduinoBLE scale(false);
    CHECK(scale.init());
    fakeMillis = FIRST_PACKET_TIMEOUT_MS - 1;
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.isConnected());
    fakeMillis = FIRST_PACKET_TIMEOUT_MS;
    CHECK(!scale.newWeightAvailable());
    CHECK(!scale.isConnected());
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::FIRST_PACKET_TIMEOUT);
    CHECK(fixture.peripheral->disconnectCalls == 1);

    resetFake();
    fixture = makeScale(NEW);
    AcaiaArduinoBLE steadyScale(false);
    CHECK(steadyScale.init());
    notify(fixture, acaiaNewWeight(123.4f));
    CHECK(steadyScale.newWeightAvailable());
    CHECK(std::fabs(steadyScale.getWeight() - 123.4f) < 0.01f);
    CHECK(steadyScale.lastValidPacketAgeMs() == 0);
    fakeMillis = MAX_PACKET_PERIOD_MS - 1;
    CHECK(!steadyScale.newWeightAvailable());
    CHECK(steadyScale.isConnected());
    fakeMillis = MAX_PACKET_PERIOD_MS;
    CHECK(!steadyScale.newWeightAvailable());
    CHECK(steadyScale.lastDisconnectReason() ==
          AcaiaDisconnectReason::PACKET_TIMEOUT);
}

void testAcaiaValidationAndDebugBounds() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    AcaiaArduinoBLE scale(true);
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
    AcaiaArduinoBLE scale(false);
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

void testCapabilitiesAndWriteCleanup() {
    resetFake();
    ScaleFixture acaia = makeScale(NEW);
    AcaiaArduinoBLE scale(false);
    CHECK(scale.init());
    CHECK(scale.heartbeatRequired());
    const size_t initializationWrites = acaia.write->writes.size();
    CHECK(!scale.tareStartTimer());
    CHECK(!scale.beep());
    CHECK(!scale.setBeepLevel(0));
    CHECK(acaia.write->writes.size() == initializationWrites);

    acaia.write->writeResult = false;
    CHECK(!scale.tare());
    CHECK(!scale.isConnected());
    CHECK(!acaia.peripheral->connected);
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::COMMAND_WRITE_FAILED);

    resetFake();
    ScaleFixture generic = makeScale(GENERIC);
    AcaiaArduinoBLE genericScale(false);
    CHECK(genericScale.init());
    CHECK(genericScale.supportsTareStartTimer());
    CHECK(genericScale.tareStartTimer());
    CHECK(genericScale.supportsIndependentBeep());
    CHECK(genericScale.beep());
    CHECK(generic.write->writes.size() == 2);
    CHECK(generic.write->writes[0][2] == 0x07);
    CHECK(generic.write->writes[1][2] == 0x02);
    CHECK(generic.write->writes[1][4] == 0x01);
    CHECK(generic.write->writes[1][5] == 0x0a);

    const size_t beforeLevels = generic.write->writes.size();
    for (uint8_t level = 0; level <= 5; ++level) {
        CHECK(genericScale.setBeepLevel(level));
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
    CHECK(genericScale.beepWithoutStateChange());
    const std::vector<byte> &beepPacket = generic.write->writes.back();
    CHECK(beepPacket == generic.write->writes[beforeLevels + 1]);
    const size_t afterValid = generic.write->writes.size();
    CHECK(!genericScale.setBeepLevel(6));
    CHECK(generic.write->writes.size() == afterValid);
}

void testOldAndGenericPacketValidation() {
    resetFake();
    ScaleFixture old = makeScale(OLD);
    AcaiaArduinoBLE oldScale(false);
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

    resetFake();
    ScaleFixture generic = makeScale(GENERIC);
    AcaiaArduinoBLE genericScale(false);
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
    AcaiaArduinoBLE scale(false);
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
    AcaiaArduinoBLE felicitaScale(false);
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
    AcaiaArduinoBLE scale(false);
    CHECK(scale.init());
    first.peripheral->connected = false;
    CHECK(!scale.isConnected());
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::REMOTE_DISCONNECTED);

    ScaleFixture second = makeScale(OLD);
    CHECK(scale.init());
    CHECK(scale.reconnectCount() == 1);
    scale.disconnect();
    CHECK(!second.peripheral->connected);
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::USER_REQUEST);
}

void testRejectedPacketsDoNotRefreshAvailability() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    AcaiaArduinoBLE scale(false);
    CHECK(scale.init());
    notify(fixture, acaiaNewWeight(10.0f));
    CHECK(scale.newWeightAvailable());

    fakeMillis = MAX_PACKET_PERIOD_MS - 1;
    std::vector<byte> invalid = acaiaNewWeight(11.0f);
    invalid[0] = 0;
    notify(fixture, invalid);
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.isConnected());

    fakeMillis = MAX_PACKET_PERIOD_MS;
    CHECK(!scale.newWeightAvailable());
    CHECK(scale.lastDisconnectReason() ==
          AcaiaDisconnectReason::PACKET_TIMEOUT);
}

void testPacketLengthCorpusAndReconnectSoak() {
    resetFake();
    ScaleFixture fixture = makeScale(NEW);
    AcaiaArduinoBLE scale(false);
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
          AcaiaDisconnectReason::INVALID_PACKET_STREAM);

    resetFake();
    AcaiaArduinoBLE reconnecting(false);
    for (int i = 0; i < 10000; ++i) {
        ScaleFixture cycle = makeScale(OLD);
        CHECK(reconnecting.init());
        cycle.peripheral->connected = false;
        CHECK(!reconnecting.isConnected());
    }
    CHECK(reconnecting.reconnectCount() == 9999);
}

} // namespace

int main() {
    testScanDiagnostics();
    testNonBlockingScanDoesNotRestartOrResetIdle();
    testNonBlockingScanConnectsWithoutInit();
    testDirectedScanUsesAddressFilter();
    testCleanupOnInitializationFailures();
    testFirstPacketAndSteadyStateTimeouts();
    testAcaiaValidationAndDebugBounds();
    testFelicitaAsciiValidation();
    testCapabilitiesAndWriteCleanup();
    testOldAndGenericPacketValidation();
    testScaleTimerParsing();
    testRemoteDisconnectAndReconnectTelemetry();
    testRejectedPacketsDoNotRefreshAvailability();
    testPacketLengthCorpusAndReconnectSoak();
    std::cout << "AcaiaArduinoBLE host tests passed: " << checks
              << " checks" << std::endl;
    return 0;
}
