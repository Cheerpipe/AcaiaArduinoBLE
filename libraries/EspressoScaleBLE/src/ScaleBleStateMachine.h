#ifndef ScaleBleStateMachine_h
#define ScaleBleStateMachine_h

#include "ScaleBleTypes.h"

enum class ScaleBleLifecycleState : uint8_t {
    Stopped,
    Idle,
    Scanning,
    Candidate,
    Connecting,
    Discovering,
    Subscribing,
    Initializing,
    Ready,
    Backoff
};

enum ScaleBleAction : uint16_t {
    ScaleBleActionNone = 0,
    ScaleBleActionStartScan = 1U << 0,
    ScaleBleActionStopScan = 1U << 1,
    ScaleBleActionConnect = 1U << 2,
    ScaleBleActionDiscover = 1U << 3,
    ScaleBleActionSubscribe = 1U << 4,
    ScaleBleActionInitialize = 1U << 5,
    ScaleBleActionPublishReady = 1U << 6,
    ScaleBleActionCleanup = 1U << 7,
    ScaleBleActionScheduleBackoff = 1U << 8
};

struct ScaleBleLifecycle {
    ScaleBleLifecycleState state;
    uint32_t generation;
    uint32_t operationId;
    uint16_t connectionHandle;
};

struct ScaleBleTransition {
    ScaleBleLifecycle next;
    uint16_t actions;
    bool accepted;
};

ScaleBleLifecycle scaleBleInitialLifecycle();
ScaleBleTransition scaleBleReduce(const ScaleBleLifecycle& current,
                                  const ScaleBleEvent& event);

#endif
