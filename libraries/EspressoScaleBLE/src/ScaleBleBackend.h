#ifndef ScaleBleBackend_h
#define ScaleBleBackend_h

enum class ScaleBleBackend {
    NimBle
};

static constexpr ScaleBleBackend kScaleBleBuildBackend = ScaleBleBackend::NimBle;

#endif
