#include "ScaleProtocol.h"

#include <math.h>

bool scaleValidWeight(float weight) {
    return isfinite(weight) && fabsf(weight) <= SCALE_MAX_WEIGHT_GRAMS;
}

uint8_t scaleXorBytes(const byte *data, int length) {
    uint8_t result = 0;
    for (int i = 0; i < length; ++i) {
        result ^= data[i];
    }
    return result;
}

uint32_t scaleReadUint32LittleEndian(const byte *data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

float scaleDecimalDivisor(byte exponent) {
    static const float divisors[] = {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f};
    const size_t count = sizeof(divisors) / sizeof(divisors[0]);
    if (exponent >= count) {
        return 1.0f;
    }
    return divisors[exponent];
}

bool scaleLooksLikeAcaiaTimer(byte minutes, byte seconds, byte tenths) {
    return minutes <= 99 && seconds <= 59 && tenths <= 9;
}

uint32_t scaleAcaiaTimerToMs(byte minutes, byte seconds, byte tenths) {
    return (static_cast<uint32_t>(minutes) * 60UL + seconds) * 1000UL +
           static_cast<uint32_t>(tenths) * 100UL;
}

bool scaleValidAcaiaChecksum(const byte *data, int length) {
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
    return checksumEven == data[length - 2] && checksumOdd == data[length - 1];
}

bool scaleFelicitaAsciiTimer(const byte *data, uint32_t *timerMs) {
    for (int i = 9; i <= 13; ++i) {
        if (data[i] < '0' || data[i] > '9') {
            return false;
        }
    }
    const byte minutes =
        static_cast<byte>((data[9] - '0') * 10 + (data[10] - '0'));
    const byte seconds =
        static_cast<byte>((data[11] - '0') * 10 + (data[12] - '0'));
    const byte tenths = static_cast<byte>(data[13] - '0');
    if (!scaleLooksLikeAcaiaTimer(minutes, seconds, tenths)) {
        return false;
    }
    *timerMs = scaleAcaiaTimerToMs(minutes, seconds, tenths);
    return true;
}
