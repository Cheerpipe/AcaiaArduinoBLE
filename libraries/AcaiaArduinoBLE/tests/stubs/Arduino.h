#ifndef ACAIA_HOST_ARDUINO_H
#define ACAIA_HOST_ARDUINO_H

#include <algorithm>
#include <cstdint>
#include <string>

using byte = uint8_t;

#ifndef HEX
#define HEX 16
#endif

extern uint32_t fakeMillis;

inline unsigned long millis() {
    return fakeMillis;
}

inline void delay(unsigned long milliseconds) {
    fakeMillis += static_cast<uint32_t>(milliseconds);
}

class String {
public:
    String() = default;
    String(const char* value) : value_(value == nullptr ? "" : value) {}
    String(const std::string& value) : value_(value) {}

    unsigned int length() const {
        return static_cast<unsigned int>(value_.length());
    }

    String substring(unsigned int from, unsigned int to) const {
        if (from >= value_.length() || to <= from) {
            return String("");
        }
        return String(value_.substr(from, std::min<unsigned int>(
            to - from, static_cast<unsigned int>(value_.length() - from))));
    }

    const char* c_str() const { return value_.c_str(); }

    bool operator==(const char* other) const {
        return value_ == (other == nullptr ? "" : other);
    }

    bool operator==(const String& other) const {
        return value_ == other.value_;
    }

private:
    std::string value_;
};

class FakeSerialClass {
public:
    template <typename T>
    void print(const T&) {}

    template <typename T>
    void print(const T&, int) {}

    template <typename T>
    void println(const T&) {}

    template <typename T>
    void println(const T&, int) {}

    void println() {}
};

extern FakeSerialClass Serial;

#endif
