#include "ScaleProtocol.h"

#include <math.h>
#include <string.h>

bool scaleValidWeight(float weight) {
    return isfinite(weight) && fabsf(weight) <= SCALE_MAX_WEIGHT_GRAMS;
}

bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *protocol) {
    if (name == 0 || name[0] == '\0' || protocol == 0 ||
        protocol->namePrefixes == 0) {
        return false;
    }
    for (size_t p = 0; p < protocol->namePrefixCount; ++p) {
        const char *prefix = protocol->namePrefixes[p];
        if (prefix == 0 || prefix[0] == '\0') {
            continue;
        }
        if (strncmp(name, prefix, strlen(prefix)) == 0) {
            return true;
        }
    }
    return false;
}

bool scaleParseUuid16(const char *uuid, uint16_t *out) {
    if (uuid == 0 || out == 0) {
        return false;
    }
    uint16_t value = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = uuid[i];
        uint8_t nibble = 0;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<uint8_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<uint8_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<uint8_t>(c - 'A' + 10);
        } else {
            return false;
        }
        value = static_cast<uint16_t>((value << 4) | nibble);
    }
    if (uuid[4] != '\0') {
        return false;
    }
    *out = value;
    return true;
}

bool scaleUuid16AllowsNamelessConnect(uint16_t uuid) {
    if (uuid == 0xFFF1 || uuid == 0xFFF2) {
        return false;
    }
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        const ScaleProtocol *protocol = scaleProtocolAt(i);
        if (protocol == 0 || protocol->requireAdvertisedName) {
            continue;
        }
        uint16_t parsed = 0;
        if (scaleParseUuid16(protocol->readUuid, &parsed) && parsed == uuid) {
            return true;
        }
        if (scaleParseUuid16(protocol->writeUuid, &parsed) && parsed == uuid) {
            return true;
        }
    }
    return false;
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
