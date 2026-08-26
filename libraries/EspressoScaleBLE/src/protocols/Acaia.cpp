#include "ScaleProtocol.h"

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

static const char *const kAcaiaPrefixes[] = {
    "CINCO", "ACAIA", "PYXIS", "LUNAR", "PEARL", "PROCH"
};

static const ScalePayload kAcaiaInitWrites[] = {
    {IDENTIFY, static_cast<int>(sizeof(IDENTIFY))},
    {NOTIFICATION_REQUEST, static_cast<int>(sizeof(NOTIFICATION_REQUEST))}
};

static const ScaleFeatureSet kAcaiaFeatures = {
    SCALE_CORE_FEATURES | ScaleFeatureHeartbeat |
        ScaleFeatureCommandAudibleFeedback,
    0,
    0,
    2750,
    5000
};

bool copyPayload(const byte *command, int commandLength, byte *out,
                 int *length) {
    if (out == 0 || length == 0 || commandLength <= 0 ||
        commandLength > SCALE_MAX_COMMAND_LENGTH) {
        return false;
    }
    for (int i = 0; i < commandLength; ++i) {
        out[i] = command[i];
    }
    *length = commandLength;
    return true;
}

bool encodeAcaiaCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_ACAIA, sizeof(TARE_ACAIA), out, length);
        case ScaleOp::StartTimer:
            return copyPayload(START_TIMER, sizeof(START_TIMER), out, length);
        case ScaleOp::StopTimer:
            return copyPayload(STOP_TIMER, sizeof(STOP_TIMER), out, length);
        case ScaleOp::ResetTimer:
            return copyPayload(RESET_TIMER, sizeof(RESET_TIMER), out, length);
        case ScaleOp::Heartbeat:
            return copyPayload(HEARTBEAT, sizeof(HEARTBEAT), out, length);
        default:
            break;
    }
    return false;
}

bool acaiaOldSupportedPacketLength(int length) {
    return length == 10 || length == 14;
}

bool acaiaNewSupportedPacketLength(int length) {
    return length == 10 || length == 13 || length == 17;
}

bool parseAcaiaOldWeight(const byte *data, int length, float *weight) {
    if ((length != 10 && length != 14) || data[6] < 1 || data[6] > 4) {
        return false;
    }
    if (length == 14 && !scaleValidAcaiaChecksum(data, length)) {
        return false;
    }

    const uint32_t raw =
        (static_cast<uint32_t>(data[3]) << 8) | data[2];
    *weight = static_cast<float>(raw) / scaleDecimalDivisor(data[6]);
    if ((data[7] & 0x02) != 0) {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool parseAcaiaOldTimer(const byte *data, int length, uint32_t *timerMs) {
    (void)data;
    (void)length;
    (void)timerMs;
    return false;
}

bool parseAcaiaNewWeight(const byte *data, int length, float *weight) {
    if ((length != 13 && length != 17) || data[0] != 0xef ||
        data[1] != 0xdd || data[2] != 0x0c ||
        static_cast<int>(data[3]) + 5 != length || data[4] != 0x05 ||
        data[9] < 1 || data[9] > 4 ||
        !scaleValidAcaiaChecksum(data, length)) {
        return false;
    }

    const uint32_t raw =
        (static_cast<uint32_t>(data[6]) << 8) | data[5];
    *weight = static_cast<float>(raw) / scaleDecimalDivisor(data[9]);
    if ((data[10] & 0x02) != 0) {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool parseAcaiaNewTimer(const byte *data, int length, uint32_t *timerMs) {
    if ((length != 10 && length != 13 && length != 17) ||
        data[0] != 0xef || data[1] != 0xdd || data[2] != 0x0c ||
        static_cast<int>(data[3]) + 5 != length ||
        !scaleValidAcaiaChecksum(data, length)) {
        return false;
    }
    if (data[4] == 0x07 &&
        scaleLooksLikeAcaiaTimer(data[5], data[6], data[7])) {
        *timerMs = scaleAcaiaTimerToMs(data[5], data[6], data[7]);
        return true;
    }
    if (data[4] == 0x05 && length == 17 &&
        scaleLooksLikeAcaiaTimer(data[11], data[12], data[13])) {
        *timerMs = scaleAcaiaTimerToMs(data[11], data[12], data[13]);
        return true;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolAcaiaLegacy = {
    "acaia_legacy",
    "Old version Acaia detected",
    "2a80",
    "2a80",
    kAcaiaPrefixes,
    sizeof(kAcaiaPrefixes) / sizeof(kAcaiaPrefixes[0]),
    kAcaiaFeatures,
    &acaiaOldSupportedPacketLength,
    &parseAcaiaOldWeight,
    &parseAcaiaOldTimer,
    &encodeAcaiaCommand,
    kAcaiaInitWrites,
    sizeof(kAcaiaInitWrites) / sizeof(kAcaiaInitWrites[0]),
    false
};

const ScaleProtocol kScaleProtocolAcaia = {
    "acaia",
    "New version Acaia detected",
    "49535343-1e4d-4bd9-ba61-23c647249616",
    "49535343-8841-43f4-a8d4-ecbe34729bb3",
    kAcaiaPrefixes,
    sizeof(kAcaiaPrefixes) / sizeof(kAcaiaPrefixes[0]),
    kAcaiaFeatures,
    &acaiaNewSupportedPacketLength,
    &parseAcaiaNewWeight,
    &parseAcaiaNewTimer,
    &encodeAcaiaCommand,
    kAcaiaInitWrites,
    sizeof(kAcaiaInitWrites) / sizeof(kAcaiaInitWrites[0]),
    false
};
