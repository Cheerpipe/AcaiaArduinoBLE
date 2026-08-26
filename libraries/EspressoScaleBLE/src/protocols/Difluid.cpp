#include "ScaleProtocol.h"

namespace {

static const byte TARE_DIFLUID[7] =
    {0xdf, 0xdf, 0x03, 0x02, 0x01, 0x01, 0xc5};
static const byte UNIT_GRAM[7] =
    {0xdf, 0xdf, 0x01, 0x04, 0x01, 0x00, 0xc4};
static const byte AUTO_NOTIFY[7] =
    {0xdf, 0xdf, 0x01, 0x00, 0x01, 0x01, 0xc1};
static const byte HEARTBEAT_DIFLUID[6] =
    {0xdf, 0xdf, 0x03, 0x05, 0x00, 0xc6};

static const char *const kDifluidPrefixes[] = {"Microbalance"};

static const ScalePayload kDifluidInitWrites[] = {
    {UNIT_GRAM, static_cast<int>(sizeof(UNIT_GRAM))},
    {AUTO_NOTIFY, static_cast<int>(sizeof(AUTO_NOTIFY))}
};

static const ScaleFeatureSet kDifluidFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureHeartbeat,
    0,
    0,
    2000,
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

uint8_t difluidChecksum(const byte *data, int length) {
    uint16_t sum = 0;
    for (int i = 0; i < length - 1; ++i) {
        sum = static_cast<uint16_t>(sum + data[i]);
    }
    return static_cast<uint8_t>(sum & 0xff);
}

bool difluidSupportedPacketLength(int length) {
    return length == 19 || length == 20;
}

bool parseDifluidWeight(const byte *data, int length, float *weight) {
    if (length < 19 || data[0] != 0xdf || data[1] != 0xdf) {
        return false;
    }
    if (difluidChecksum(data, length) != data[length - 1]) {
        return false;
    }
    const byte func = data[2];
    const byte cmd = data[3];
    const byte dataLen = data[4];
    if (func != 0x03 || cmd != 0x00 || dataLen < 13 ||
        length < 6 + static_cast<int>(dataLen)) {
        return false;
    }
    const int32_t raw =
        (static_cast<int32_t>(data[5]) << 24) |
        (static_cast<int32_t>(data[6]) << 16) |
        (static_cast<int32_t>(data[7]) << 8) | data[8];
    *weight = static_cast<float>(raw) / 10.0f;
    return scaleValidWeight(*weight);
}

bool encodeDifluidCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_DIFLUID, sizeof(TARE_DIFLUID), out,
                               length);
        case ScaleOp::Heartbeat:
            return copyPayload(HEARTBEAT_DIFLUID, sizeof(HEARTBEAT_DIFLUID),
                               out, length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolDifluid = {
    "difluid",
    "DiFluid Microbalance detected",
    "aa01",
    "aa01",
    kDifluidPrefixes,
    sizeof(kDifluidPrefixes) / sizeof(kDifluidPrefixes[0]),
    kDifluidFeatures,
    &difluidSupportedPacketLength,
    &parseDifluidWeight,
    0,
    &encodeDifluidCommand,
    kDifluidInitWrites,
    sizeof(kDifluidInitWrites) / sizeof(kDifluidInitWrites[0]),
    false
};
