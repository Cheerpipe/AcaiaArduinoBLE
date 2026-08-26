#include "ScaleProtocol.h"

static const ScaleProtocol *const kProtocols[] = {
    &kScaleProtocolAcaiaLegacy,
    &kScaleProtocolAcaia,
    &kScaleProtocolGenericFf11,
    &kScaleProtocolFelicita,
    &kScaleProtocolEclair,
    &kScaleProtocolDecent,
    &kScaleProtocolDifluid,
    &kScaleProtocolMyscale,
    &kScaleProtocolWeighMyBru,
    &kScaleProtocolVaria,
    &kScaleProtocolEureka,
};

const ScaleProtocol *scaleProtocolAt(size_t index) {
    if (index >= scaleProtocolCount()) {
        return 0;
    }
    return kProtocols[index];
}

size_t scaleProtocolCount() {
    return sizeof(kProtocols) / sizeof(kProtocols[0]);
}

bool scaleNameIsCompatible(const char *name) {
    if (name == 0 || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < scaleProtocolCount(); ++i) {
        if (scaleNameMatchesProtocol(name, kProtocols[i])) {
            return true;
        }
    }
    return false;
}
