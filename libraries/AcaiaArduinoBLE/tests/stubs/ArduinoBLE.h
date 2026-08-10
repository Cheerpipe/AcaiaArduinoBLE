#ifndef ACAIA_HOST_ARDUINO_BLE_H
#define ACAIA_HOST_ARDUINO_BLE_H

#include "Arduino.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FakeBLE {

struct CharacteristicState {
    std::string uuid;
    bool readable = false;
    bool writable = true;
    bool subscribable = false;
    bool subscribeResult = true;
    bool writeResult = true;
    bool updated = false;
    int forcedReadLength = -1;
    std::vector<byte> value;
    std::vector<std::vector<byte> > writes;
};

struct PeripheralState {
    std::string address = "01:02:03:04:05:06";
    std::string localName;
    bool connectResult = true;
    bool discoveryResult = true;
    bool connected = false;
    int connectCalls = 0;
    int disconnectCalls = 0;
    std::map<std::string, std::shared_ptr<CharacteristicState> >
        characteristics;
};

} // namespace FakeBLE

class BLEDescriptor {
public:
    const char* uuid() const { return ""; }
    bool read() { return true; }
    const uint8_t* value() const { return nullptr; }
    int valueLength() const { return 0; }
};

class BLECharacteristic {
public:
    BLECharacteristic() = default;
    explicit BLECharacteristic(
        const std::shared_ptr<FakeBLE::CharacteristicState>& state) :
        state_(state) {}
    BLECharacteristic(const BLECharacteristic& other) : state_(other.state_) {}
    ~BLECharacteristic() = default;

    // Models ArduinoBLE 2.1.0's unsafe operation as unavailable. Host tests
    // therefore fail to compile if the library regresses to copy assignment.
    BLECharacteristic& operator=(const BLECharacteristic&) = delete;

    explicit operator bool() const { return static_cast<bool>(state_); }
    const char* uuid() const { return state_ ? state_->uuid.c_str() : ""; }
    uint8_t properties() const { return 0; }
    bool canRead() { return state_ && state_->readable; }
    bool read() { return canRead(); }
    bool canWrite() { return state_ && state_->writable; }
    bool canSubscribe() { return state_ && state_->subscribable; }
    bool subscribe() {
        return state_ && state_->subscribable && state_->subscribeResult;
    }
    bool valueUpdated() {
        if (!state_) {
            return false;
        }
        const bool result = state_->updated;
        state_->updated = false;
        return result;
    }
    int valueLength() const {
        return state_ ? static_cast<int>(state_->value.size()) : 0;
    }
    const uint8_t* value() const {
        return state_ && !state_->value.empty() ? state_->value.data()
                                                 : nullptr;
    }
    int readValue(uint8_t* destination, int length) {
        if (!state_ || destination == nullptr || length < 0) {
            return 0;
        }
        int count = std::min(length, static_cast<int>(state_->value.size()));
        if (state_->forcedReadLength >= 0) {
            count = std::min(count, state_->forcedReadLength);
        }
        if (count > 0) {
            std::memcpy(destination, state_->value.data(),
                        static_cast<size_t>(count));
        }
        return count;
    }
    int writeValue(const uint8_t* value, int length,
                   bool withResponse = true) {
        (void)withResponse;
        if (!state_ || !state_->writable || !state_->writeResult ||
            value == nullptr || length <= 0) {
            return 0;
        }
        state_->writes.push_back(
            std::vector<byte>(value, value + static_cast<size_t>(length)));
        return 1;
    }
    int descriptorCount() const { return 0; }
    BLEDescriptor descriptor(int) const { return BLEDescriptor(); }

private:
    std::shared_ptr<FakeBLE::CharacteristicState> state_;
};

class BLEService {
public:
    const char* uuid() const { return ""; }
    int characteristicCount() const { return 0; }
    BLECharacteristic characteristic(int) const {
        return BLECharacteristic();
    }
};

class BLEDevice {
public:
    BLEDevice() = default;
    explicit BLEDevice(const std::shared_ptr<FakeBLE::PeripheralState>& state) :
        state_(state) {}

    explicit operator bool() const { return static_cast<bool>(state_); }
    bool connect() {
        if (!state_) {
            return false;
        }
        ++state_->connectCalls;
        state_->connected = state_->connectResult;
        return state_->connected;
    }
    bool connected() const { return state_ && state_->connected; }
    bool disconnect() {
        if (!state_) {
            return false;
        }
        ++state_->disconnectCalls;
        const bool wasConnected = state_->connected;
        state_->connected = false;
        return wasConnected;
    }
    bool discoverAttributes() {
        return state_ && state_->connected && state_->discoveryResult;
    }
    String address() const {
        return String(state_ ? state_->address : "");
    }
    String localName() const {
        return String(state_ ? state_->localName : "");
    }
    String advertisedServiceUuid() const { return String(""); }
    String deviceName() const { return localName(); }
    int appearance() const { return 0; }
    int serviceCount() const { return 0; }
    BLEService service(int) const { return BLEService(); }
    BLECharacteristic characteristic(const char* uuid) const {
        if (!state_ || uuid == nullptr) {
            return BLECharacteristic();
        }
        const auto found = state_->characteristics.find(uuid);
        return found == state_->characteristics.end()
            ? BLECharacteristic()
            : BLECharacteristic(found->second);
    }

private:
    std::shared_ptr<FakeBLE::PeripheralState> state_;
};

class BLEClass {
public:
    int scan() {
        delivered_ = false;
        scanning_ = scanResult;
        return scanResult ? 1 : 0;
    }
    int scanForAddress(String) { return scan(); }
    void stopScan() {
        scanning_ = false;
        ++stopScanCalls;
    }
    BLEDevice available() {
        if (!scanning_ || delivered_ || !availableState) {
            return BLEDevice();
        }
        delivered_ = true;
        return BLEDevice(availableState);
    }
    void poll() {}
    void setTimeout(unsigned long timeout) { timeoutMs = timeout; }
    bool disconnect() {
        if (!availableState) {
            return false;
        }
        return BLEDevice(availableState).disconnect();
    }
    void setAvailable(
        const std::shared_ptr<FakeBLE::PeripheralState>& peripheral) {
        availableState = peripheral;
        delivered_ = false;
    }
    void reset() {
        scanResult = true;
        scanning_ = false;
        delivered_ = false;
        stopScanCalls = 0;
        timeoutMs = 0;
        availableState.reset();
    }

    bool scanResult = true;
    int stopScanCalls = 0;
    unsigned long timeoutMs = 0;
    std::shared_ptr<FakeBLE::PeripheralState> availableState;

private:
    bool scanning_ = false;
    bool delivered_ = false;
};

extern BLEClass BLE;

#endif
