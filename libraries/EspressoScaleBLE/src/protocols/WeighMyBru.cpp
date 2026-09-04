#include "ScaleProtocol.h"

namespace {

static const uint8_t TARE_WEIGHMYBRU[6] =
    {0x03, 0x0a, 0x01, 0x01, 0x00, 0x09};

static const char *const kWeighMyBruPrefixes[] = {"WeighMyBru"};

static const ScaleFeatureSet kWeighMyBruFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare,
    0,
    0,
    0,
    8000
};

bool copyPayload(const uint8_t *command, int commandLength, uint8_t *out,
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

bool weighMyBruSupportedPacketLength(int length) {
    return length == 20;
}

bool parseWeighMyBruWeight(const uint8_t *data, int length, float *weight) {
    if (length != 20 || data[0] != 0x03 || data[1] != 0x0b) {
        return false;
    }
    if (scaleXorBytes(data, 19) != data[19]) {
        return false;
    }
    const uint32_t raw = (static_cast<uint32_t>(data[7]) << 16) |
                         (static_cast<uint32_t>(data[8]) << 8) | data[9];
    *weight = static_cast<float>(raw) / 100.0f;
    if (data[6] == '-') {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool encodeWeighMyBruCommand(ScaleOp op, uint8_t arg, uint8_t *out, int *length) {
    (void)arg;
    if (op == ScaleOp::Tare) {
        return copyPayload(TARE_WEIGHMYBRU, sizeof(TARE_WEIGHMYBRU), out,
                           length);
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolWeighMyBru = {
    "weighmybru",
    "WeighMyBru detected",
    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E",
    kWeighMyBruPrefixes,
    sizeof(kWeighMyBruPrefixes) / sizeof(kWeighMyBruPrefixes[0]),
    kWeighMyBruFeatures,
    &weighMyBruSupportedPacketLength,
    &parseWeighMyBruWeight,
    0,
    &encodeWeighMyBruCommand,
    0,
    0,
    false
};
