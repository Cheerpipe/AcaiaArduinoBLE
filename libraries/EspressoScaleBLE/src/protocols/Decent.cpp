#include "ScaleProtocol.h"

namespace {

static const byte TARE_DECENT[7] =
    {0x03, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x0d};
static const byte HEARTBEAT_DECENT[7] =
    {0x03, 0x0a, 0x03, 0xff, 0xff, 0x00, 0x0a};

static const char *const kDecentPrefixes[] = {
    "Decent Scale",
    "EspressiScale"
};

static const ScaleFeatureSet kDecentFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureHeartbeat,
    0,
    0,
    5000,
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

bool decentSupportedPacketLength(int length) {
    return length == 7 || length == 10;
}

bool parseDecentWeight(const byte *data, int length, float *weight) {
    if ((length != 7 && length != 10) || data[0] != 0x03 ||
        (data[1] != 0xca && data[1] != 0xce)) {
        return false;
    }
    const byte xorByte = data[length - 1];
    if (xorByte != 0 && scaleXorBytes(data, length - 1) != xorByte) {
        return false;
    }
    const int16_t raw = static_cast<int16_t>(
        (static_cast<uint16_t>(data[2]) << 8) | data[3]);
    *weight = static_cast<float>(raw) / 10.0f;
    return scaleValidWeight(*weight);
}

bool encodeDecentCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    (void)arg;
    switch (op) {
        case ScaleOp::Tare:
            return copyPayload(TARE_DECENT, sizeof(TARE_DECENT), out, length);
        case ScaleOp::Heartbeat:
            return copyPayload(HEARTBEAT_DECENT, sizeof(HEARTBEAT_DECENT), out,
                               length);
        default:
            break;
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolDecent = {
    "decent",
    "Decent Scale detected",
    "fff4",
    "36f5",
    kDecentPrefixes,
    sizeof(kDecentPrefixes) / sizeof(kDecentPrefixes[0]),
    kDecentFeatures,
    &decentSupportedPacketLength,
    &parseDecentWeight,
    0,
    &encodeDecentCommand,
    0,
    0,
    false
};
