#ifndef ScaleProtocol_h
#define ScaleProtocol_h

#include "Arduino.h"
#include "ScaleFeatures.h"

#define SCALE_MAX_PACKET_LENGTH 20
#define SCALE_MAX_COMMAND_LENGTH 20
#define SCALE_MAX_WEIGHT_GRAMS 10000.0f
#define SCALE_CORE_FEATURES                                                    \
    (ScaleFeatureWeight | ScaleFeatureTare | ScaleFeatureStartTimer |          \
     ScaleFeatureStopTimer | ScaleFeatureResetTimer)

struct ScalePayload {
    const byte *data;
    int length;
};

struct ScaleProtocol {
    const char *id;
    const char *debugLabel;
    const char *readUuid;
    const char *writeUuid;
    const char *const *namePrefixes;
    size_t namePrefixCount;
    ScaleFeatureSet features;
    bool (*supportedPacketLength)(int length);
    bool (*parseWeight)(const byte *data, int length, float *weight);
    bool (*parseTimer)(const byte *data, int length, uint32_t *timerMs);
    bool (*encodeCommand)(ScaleOp op, uint8_t arg, byte *out, int *length);
    const ScalePayload *initWrites;
    size_t initWriteCount;
    bool requireAdvertisedName;
};

const ScaleProtocol *scaleProtocolAt(size_t index);
size_t scaleProtocolCount();
bool scaleNameIsCompatible(const char *name);
bool scaleNameMatchesProtocol(const char *name, const ScaleProtocol *protocol);
bool scaleParseUuid16(const char *uuid, uint16_t *out);
bool scaleUuid16AllowsNamelessConnect(uint16_t uuid);

bool scaleValidWeight(float weight);
uint8_t scaleXorBytes(const byte *data, int length);
uint32_t scaleReadUint32LittleEndian(const byte *data);
float scaleDecimalDivisor(byte exponent);
bool scaleLooksLikeAcaiaTimer(byte minutes, byte seconds, byte tenths);
uint32_t scaleAcaiaTimerToMs(byte minutes, byte seconds, byte tenths);
bool scaleValidAcaiaChecksum(const byte *data, int length);
bool scaleFelicitaAsciiTimer(const byte *data, uint32_t *timerMs);

extern const ScaleProtocol kScaleProtocolAcaiaLegacy;
extern const ScaleProtocol kScaleProtocolAcaia;
extern const ScaleProtocol kScaleProtocolGenericFf11;
extern const ScaleProtocol kScaleProtocolFelicita;
extern const ScaleProtocol kScaleProtocolEclair;
extern const ScaleProtocol kScaleProtocolDecent;
extern const ScaleProtocol kScaleProtocolDifluid;
extern const ScaleProtocol kScaleProtocolMyscale;
extern const ScaleProtocol kScaleProtocolWeighMyBru;
extern const ScaleProtocol kScaleProtocolVaria;
extern const ScaleProtocol kScaleProtocolEureka;

#endif
