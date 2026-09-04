#include "ScaleBleStateMachine.h"

namespace {

uint32_t nextGeneration(uint32_t generation) {
    ++generation;
    return generation == 0 ? 1U : generation;
}

ScaleBleTransition rejected(const ScaleBleLifecycle& current) {
    ScaleBleTransition result = {current, ScaleBleActionNone, false};
    return result;
}

ScaleBleTransition accepted(const ScaleBleLifecycle& current,
                            ScaleBleLifecycleState state, uint16_t actions) {
    ScaleBleTransition result = {current, actions, true};
    result.next.state = state;
    return result;
}

bool isCurrentOperation(const ScaleBleLifecycle& current,
                        const ScaleBleEvent& event) {
    return event.generation == current.generation &&
           event.operationId != 0 && current.operationId != 0 &&
           event.operationId == current.operationId;
}

ScaleBleTransition enterBackoff(const ScaleBleLifecycle& current) {
    ScaleBleTransition result = accepted(
        current, ScaleBleLifecycleState::Backoff,
        ScaleBleActionCleanup | ScaleBleActionScheduleBackoff);
    // Invalidate the failed lifecycle at the cleanup edge. A late GAP/GATT
    // callback can therefore never be accepted while backoff is pending.
    result.next.generation = nextGeneration(current.generation);
    result.next.connectionHandle = SCALE_BLE_INVALID_CONNECTION_HANDLE;
    result.next.operationId = 0;
    return result;
}

} // namespace

ScaleBleLifecycle scaleBleInitialLifecycle() {
    ScaleBleLifecycle lifecycle = {
        ScaleBleLifecycleState::Idle,
        0,
        0,
        SCALE_BLE_INVALID_CONNECTION_HANDLE
    };
    return lifecycle;
}

ScaleBleTransition scaleBleReduce(const ScaleBleLifecycle& current,
                                  const ScaleBleEvent& event) {
    if (event.type == ScaleBleEventType::StopRequested) {
        ScaleBleTransition result = accepted(
            current, ScaleBleLifecycleState::Stopped, ScaleBleActionCleanup);
        result.next.generation = nextGeneration(current.generation);
        result.next.operationId = 0;
        result.next.connectionHandle = SCALE_BLE_INVALID_CONNECTION_HANDLE;
        return result;
    }

    if (event.type == ScaleBleEventType::StartRequested) {
        if (current.state != ScaleBleLifecycleState::Idle &&
            current.state != ScaleBleLifecycleState::Stopped) {
            return rejected(current);
        }
        ScaleBleTransition result = accepted(
            current, ScaleBleLifecycleState::Scanning,
            ScaleBleActionStartScan);
        result.next.generation = nextGeneration(current.generation);
        result.next.operationId = event.operationId;
        result.next.connectionHandle = SCALE_BLE_INVALID_CONNECTION_HANDLE;
        return result;
    }

    if (current.state == ScaleBleLifecycleState::Backoff &&
        event.type == ScaleBleEventType::BackoffExpired &&
        event.generation == current.generation && event.operationId != 0) {
        ScaleBleTransition result = accepted(
            current, ScaleBleLifecycleState::Scanning,
            ScaleBleActionStartScan);
        result.next.generation = nextGeneration(current.generation);
        result.next.operationId = event.operationId;
        return result;
    }

    if (!isCurrentOperation(current, event)) {
        return rejected(current);
    }

    switch (current.state) {
        case ScaleBleLifecycleState::Scanning:
            if (event.type == ScaleBleEventType::ScanStarted) {
                return accepted(current, current.state, ScaleBleActionNone);
            }
            if (event.type == ScaleBleEventType::CompatibleAdvertisement) {
                ScaleBleTransition result = accepted(
                    current, ScaleBleLifecycleState::Candidate,
                    ScaleBleActionStopScan | ScaleBleActionConnect);
                result.next.operationId = event.operationId;
                return result;
            }
            break;

        case ScaleBleLifecycleState::Candidate:
            if (event.type == ScaleBleEventType::ConnectIssued) {
                return accepted(current, ScaleBleLifecycleState::Connecting,
                                ScaleBleActionNone);
            }
            break;

        case ScaleBleLifecycleState::Connecting:
            if (event.type == ScaleBleEventType::Connected) {
                ScaleBleTransition result = accepted(
                    current, ScaleBleLifecycleState::Discovering,
                    ScaleBleActionDiscover);
                result.next.connectionHandle = event.connectionHandle;
                return result;
            }
            break;

        case ScaleBleLifecycleState::Discovering:
            if (event.type == ScaleBleEventType::DiscoveryComplete) {
                return accepted(current, ScaleBleLifecycleState::Subscribing,
                                ScaleBleActionSubscribe);
            }
            break;

        case ScaleBleLifecycleState::Subscribing:
            if (event.type == ScaleBleEventType::SubscriptionComplete) {
                return accepted(current, ScaleBleLifecycleState::Initializing,
                                ScaleBleActionInitialize);
            }
            break;

        case ScaleBleLifecycleState::Initializing:
            if (event.type == ScaleBleEventType::InitializationComplete) {
                return accepted(current, ScaleBleLifecycleState::Ready,
                                ScaleBleActionPublishReady);
            }
            break;

        case ScaleBleLifecycleState::Backoff:
            break;

        case ScaleBleLifecycleState::Stopped:
        case ScaleBleLifecycleState::Idle:
        case ScaleBleLifecycleState::Ready:
            break;
    }

    if (event.type == ScaleBleEventType::Disconnected ||
        event.type == ScaleBleEventType::OperationFailed ||
        event.type == ScaleBleEventType::DeadlineExpired) {
        if (current.state != ScaleBleLifecycleState::Idle &&
            current.state != ScaleBleLifecycleState::Stopped &&
            current.state != ScaleBleLifecycleState::Backoff) {
            return enterBackoff(current);
        }
    }

    return rejected(current);
}
