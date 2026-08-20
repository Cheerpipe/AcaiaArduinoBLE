#define SHOT_STOPPER_HOST_TEST
#define ARDUINO_ESP32S3_DEV
#define SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO 4
#define SHOT_STOPPER_CN9_FEEDBACK_GPIO 5

#include <cstdlib>
#include <iostream>

#include "../shotStopper.cpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::cerr << __func__ << ":" << __LINE__                            \
                << ": check failed: " << #condition << "\n";             \
      ++failures;                                                          \
      return;                                                              \
    }                                                                      \
  } while (false)

void releaseResources() {
  delete relaySafetyTimer;
  delete operationalLimitTimer;
  relaySafetyTimer = nullptr;
  operationalLimitTimer = nullptr;
  independentSafetyTimer.resetForHost();
}

void resetSafety() {
  releaseResources();
  hostMillis = 0;
  hostPinLevel.fill(HIGH);
  hostPinMode.fill(0);
  hostTrackedRelayPin = RELAY_GPIO;
  hostTrackedRelayOpenLevel = RELAY_OPEN_LEVEL;
  hostTrackedRelayClosedLevel = RELAY_CLOSED_LEVEL;
  hostRelayOpenWrites = 0;
  hostRelayClosedWrites = 0;
  hostEspTimerCreateSucceeds = true;
  hostEspTimerStartSucceeds = true;
  hostGptimerCreateSucceeds = true;
  hostGptimerArmSucceeds = true;
  hostTaskWatchdogOperationsSucceed = true;
  hostTaskWatchdogConfigured = false;
  hostTaskWatchdogSubscriptions = 0;
  hostTaskWatchdogFeeds = 0;
  hostCn9ArmBeforeCommitHook = nullptr;
  resetSafetyResetGuardForHost();

  cn9Closed = false;
  relaySafetyTripped = false;
  operationalLimitTripped = false;
  cn9ClosedAtMs = 0;
  operationalLimitAtArmMs = HARD_MAX_CN9_CLOSED_MS;
  relaySafetyState = RelaySafetyState::OPEN;
  relaySafetyFault = RelaySafetyFault::NONE;
  relaySafetyGeneration = 0;
  criticalTaskWatchdogFault = false;
  feedbackTransitionPending = false;
  feedbackExpectedClosed = false;
  feedbackTransitionStartedAtMs = 0;
  feedbackTransitionStampPending = false;
  safetyHeartbeatLevel = false;
  safetyHeartbeatToggledAtMs = 0;
  safeRestartRequested = false;
  platformClockReady = true;
  safetyResetStatus = SafetyResetSnapshot{};

  digitalWrite(RELAY_GPIO, RELAY_OPEN_LEVEL);
  pinMode(RELAY_GPIO, OUTPUT);
  pinMode(CN9_FEEDBACK_GPIO, INPUT_PULLUP);
  digitalWrite(SAFETY_HEARTBEAT_GPIO, LOW);
  pinMode(SAFETY_HEARTBEAT_GPIO, OUTPUT);
  taskWatchdogReady =
      configureTaskWatchdog() && subscribeCurrentTaskToWatchdog();
  relaySafetyTimersReady = initializeRelaySafetyTimer();
  CHECK(taskWatchdogReady);
  CHECK(relaySafetyTimersReady);
}

void feedback_tracks_a_healthy_close_then_detects_contact_loss() {
  resetSafety();
  CHECK(setCn9Closed(true, 5000));
  hostPinLevel[CN9_FEEDBACK_GPIO] = CN9_FEEDBACK_CLOSED_LEVEL;
  hostMillis += CN9_FEEDBACK_SETTLE_MS;
  serviceRelaySafety();
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::CLOSED);

  hostPinLevel[CN9_FEEDBACK_GPIO] = !CN9_FEEDBACK_CLOSED_LEVEL;
  serviceRelaySafety();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::FEEDBACK_CHANGED_UNEXPECTEDLY);
  CHECK(hostPinLevel[RELAY_GPIO] == RELAY_OPEN_LEVEL);
}

void stuck_closed_feedback_blocks_every_close_attempt() {
  resetSafety();
  hostPinLevel[CN9_FEEDBACK_GPIO] = CN9_FEEDBACK_CLOSED_LEVEL;
  CHECK(!setCn9Closed(true, 5000));
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
  CHECK(hostRelayClosedWrites == 0);
  CHECK(!setCn9Closed(true, 5000));
  CHECK(getRelaySafetySnapshot().fault ==
        RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
}

void missing_close_feedback_opens_and_locks_out() {
  resetSafety();
  CHECK(setCn9Closed(true, 5000));
  hostMillis += CN9_FEEDBACK_SETTLE_MS;
  serviceRelaySafety();
  const RelaySafetySnapshot relay = getRelaySafetySnapshot();
  CHECK(!relay.closed);
  CHECK(relay.state == RelaySafetyState::LOCKOUT);
  CHECK(relay.fault == RelaySafetyFault::FEEDBACK_FAILED_TO_CLOSE);
}

void heartbeat_is_emitted_only_after_healthy_loop_epochs() {
  resetSafety();
  serviceSafetyHeartbeat(true);
  CHECK(hostPinLevel[SAFETY_HEARTBEAT_GPIO] == LOW);
  hostMillis += SAFETY_HEARTBEAT_TOGGLE_MS;
  serviceSafetyHeartbeat(true);
  CHECK(hostPinLevel[SAFETY_HEARTBEAT_GPIO] == HIGH);
  serviceSafetyHeartbeat(false);
  CHECK(hostPinLevel[SAFETY_HEARTBEAT_GPIO] == LOW);
}

void gptimer_open_does_not_false_stuck_closed_before_settle() {
  resetSafety();
  CHECK(setCn9Closed(true, 5000));
  hostPinLevel[CN9_FEEDBACK_GPIO] = CN9_FEEDBACK_CLOSED_LEVEL;
  hostMillis += CN9_FEEDBACK_SETTLE_MS;
  serviceRelaySafety();
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::CLOSED);

  hostMillis += 5000;
  independentSafetyTimer.serviceForHost();
  CHECK(!getRelaySafetySnapshot().closed);
  CHECK(getRelaySafetySnapshot().state == RelaySafetyState::TRIPPED);

  serviceRelaySafety();
  CHECK(getRelaySafetySnapshot().fault !=
        RelaySafetyFault::FEEDBACK_STUCK_CLOSED);

  hostMillis += CN9_FEEDBACK_SETTLE_MS;
  serviceRelaySafety();
  const RelaySafetySnapshot afterSettle = getRelaySafetySnapshot();
  CHECK(afterSettle.state == RelaySafetyState::LOCKOUT);
  CHECK(afterSettle.fault == RelaySafetyFault::FEEDBACK_STUCK_CLOSED);
}

}  // namespace

int main() {
  feedback_tracks_a_healthy_close_then_detects_contact_loss();
  stuck_closed_feedback_blocks_every_close_attempt();
  missing_close_feedback_opens_and_locks_out();
  heartbeat_is_emitted_only_after_healthy_loop_epochs();
  gptimer_open_does_not_false_stuck_closed_before_settle();
  releaseResources();
  std::cout << "External CN9 safety: 5 tests, " << failures
            << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
