#include "ScaleBleBackend.h"
#include "ScaleBleClock.h"
#include "ScaleBleStateMachine.h"
#include "ScaleBleTypes.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

int checks = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(condition)) {                                                    \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__    \
                      << ": " #condition << std::endl;                        \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

static_assert(std::is_trivially_copyable<ScaleBleAddress>::value,
              "BLE address DTO must remain trivially copyable");
static_assert(std::is_trivially_copyable<ScaleBleEvent>::value,
              "BLE event DTO must remain trivially copyable");
static_assert(std::is_trivially_copyable<ScaleBleLifecycle>::value,
              "BLE lifecycle must remain trivially copyable");
static_assert(sizeof(ScaleBleEvent) <= 20,
              "BLE event DTO must stay suitable for a fixed queue");
static_assert(kScaleBleBuildBackend == ScaleBleBackend::ArduinoBle,
              "host/default build must remain ArduinoBLE in phase 1");

uint32_t fakeNow(void *context) {
    return *static_cast<uint32_t *>(context);
}

ScaleBleEvent event(ScaleBleEventType type, uint32_t generation,
                    uint32_t operationId = 0,
                    uint16_t handle = SCALE_BLE_INVALID_CONNECTION_HANDLE,
                    int32_t rawStatus = 0) {
    ScaleBleEvent value = {type, generation, operationId, handle, rawStatus};
    return value;
}

void testClockAndDeadline() {
    uint32_t now = 17;
    ScaleBleClock clock = {&fakeNow, &now};
    CHECK(clock.valid());
    CHECK(clock.now() == 17);

    ScaleBleDeadline deadline = {10, 8, true};
    CHECK(!deadline.expired(clock.now()));
    now = 18;
    CHECK(deadline.expired(clock.now()));

    // Unsigned subtraction is deliberately wrap-safe for intervals < 2^31.
    deadline.startedAtMs = 0xfffffff0U;
    deadline.timeoutMs = 0x20U;
    now = 0x0fU;
    CHECK(!deadline.expired(clock.now()));
    now = 0x10U;
    CHECK(deadline.expired(clock.now()));
}

void testHappyPathAndActions() {
    ScaleBleLifecycle state = scaleBleInitialLifecycle();
    CHECK(state.state == ScaleBleLifecycleState::Idle);
    CHECK(state.connectionHandle == SCALE_BLE_INVALID_CONNECTION_HANDLE);

    ScaleBleTransition transition =
        scaleBleReduce(state, event(ScaleBleEventType::StartRequested, 0, 7));
    CHECK(transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Scanning);
    CHECK(transition.next.generation == 1);
    CHECK(transition.actions == ScaleBleActionStartScan);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::CompatibleAdvertisement, 1, 7));
    CHECK(transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Candidate);
    CHECK((transition.actions & ScaleBleActionStopScan) != 0);
    CHECK((transition.actions & ScaleBleActionConnect) != 0);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::ConnectIssued, 1, 7));
    CHECK(transition.next.state == ScaleBleLifecycleState::Connecting);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::Connected, 1, 7, 42));
    CHECK(transition.next.state == ScaleBleLifecycleState::Discovering);
    CHECK(transition.next.connectionHandle == 42);
    CHECK(transition.actions == ScaleBleActionDiscover);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::DiscoveryComplete, 1, 7, 42));
    CHECK(transition.next.state == ScaleBleLifecycleState::Subscribing);
    CHECK(transition.actions == ScaleBleActionSubscribe);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::SubscriptionComplete, 1, 7, 42));
    CHECK(transition.next.state == ScaleBleLifecycleState::Initializing);
    CHECK(transition.actions == ScaleBleActionInitialize);
    state = transition.next;

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::InitializationComplete, 1, 7, 42));
    CHECK(transition.next.state == ScaleBleLifecycleState::Ready);
    CHECK(transition.actions == ScaleBleActionPublishReady);
}

void testStaleAndFailureEvents() {
    ScaleBleLifecycle state = {
        ScaleBleLifecycleState::Connecting, 9, 22,
        SCALE_BLE_INVALID_CONNECTION_HANDLE
    };
    ScaleBleTransition transition = scaleBleReduce(
        state, event(ScaleBleEventType::Connected, 8, 22, 3));
    CHECK(!transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Connecting);

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::Connected, 9, 21, 3));
    CHECK(!transition.accepted);

    transition = scaleBleReduce(
        state, event(ScaleBleEventType::OperationFailed, 9, 22,
                     SCALE_BLE_INVALID_CONNECTION_HANDLE, 0x3e));
    CHECK(transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Backoff);
    CHECK(transition.next.operationId == 0);
    CHECK((transition.actions & ScaleBleActionCleanup) != 0);
    CHECK((transition.actions & ScaleBleActionScheduleBackoff) != 0);

    state = transition.next;
    transition = scaleBleReduce(
        state, event(ScaleBleEventType::BackoffExpired, 9, 23));
    CHECK(transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Scanning);
    CHECK(transition.next.generation == 10);
    CHECK(transition.next.operationId == 23);

    state = transition.next;
    transition = scaleBleReduce(
        state, event(ScaleBleEventType::StopRequested, 0));
    CHECK(transition.accepted);
    CHECK(transition.next.state == ScaleBleLifecycleState::Stopped);
    CHECK(transition.next.connectionHandle ==
          SCALE_BLE_INVALID_CONNECTION_HANDLE);
    CHECK(transition.actions == ScaleBleActionCleanup);
}

} // namespace

int main() {
    testClockAndDeadline();
    testHappyPathAndActions();
    testStaleAndFailureEvents();
    std::cout << "Scale BLE portable tests passed: " << checks << " checks"
              << std::endl;
    return 0;
}
