#include "ScaleProtocol.h"

namespace {

static const byte TARE_MYSCALE[20] = {
    0xac, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xd2, 0xd2
};

static const char *const kMyscalePrefixes[] = {
    "blackcoffee",
    "my_scale",
    "MY_SCALE"
};

static const ScaleFeatureSet kMyscaleFeatures = {
    ScaleFeatureWeight | ScaleFeatureTare,
    0,
    0,
    0,
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

bool myscaleSupportedPacketLength(int length) {
    return length >= 15 && length <= SCALE_MAX_PACKET_LENGTH;
}

bool parseMyscaleWeight(const byte *data, int length, float *weight) {
    if (length < 15) {
        return false;
    }
    const byte nibble = static_cast<byte>(data[2] >> 4);
    const bool negative = nibble == 0x08 || nibble == 0x0c;
    const uint32_t raw = (static_cast<uint32_t>(data[3] & 0x0f) << 24) |
                         (static_cast<uint32_t>(data[4]) << 16) |
                         (static_cast<uint32_t>(data[5]) << 8) | data[6];
    *weight = static_cast<float>(raw) / 1000.0f;
    if (negative) {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool encodeMyscaleCommand(ScaleOp op, uint8_t arg, byte *out, int *length) {
    (void)arg;
    if (op == ScaleOp::Tare) {
        return copyPayload(TARE_MYSCALE, sizeof(TARE_MYSCALE), out, length);
    }
    return false;
}

} // namespace

const ScaleProtocol kScaleProtocolMyscale = {
    "myscale",
    "MyScale detected",
    "ffb2",
    "ffb1",
    kMyscalePrefixes,
    sizeof(kMyscalePrefixes) / sizeof(kMyscalePrefixes[0]),
    kMyscaleFeatures,
    &myscaleSupportedPacketLength,
    &parseMyscaleWeight,
    0,
    &encodeMyscaleCommand,
    0,
    0,
    false
};
