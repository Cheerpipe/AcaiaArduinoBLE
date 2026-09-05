#pragma once

#include "ShotStopperScaleLink.h"
#include "ShotStopperDomain.h"

#if !defined(SHOT_STOPPER_HOST_TEST)
#include <EspressoScaleBLE.h>
#endif

namespace shotstopper {

// Worker timing. Keep identical to the values previously in shotStopper.cpp.
constexpr uint32_t SCALE_CONNECT_LOG_MS = 10000;
constexpr uint32_t SCALE_PACKET_GAP_LOG_MIN_MS = 1000;
constexpr uint32_t SCALE_STREAM_GAP_MS = 250;
constexpr uint32_t SCALE_DISCOVERY_TICK_MS = 3000;
constexpr uint32_t SCALE_SCAN_HCI_RESTART_MS = 60000;
constexpr uint32_t SCALE_HUNT_RF_CLEAR_MS = 3000;

inline void bleScanHciParams(BleScanIntensity intensity, uint16_t &interval,
                             uint16_t &window) {
  switch (intensity) {
    case BleScanIntensity::AGGRESSIVE:
      interval = BLE_SCAN_AGGRESSIVE_INTERVAL;
      window = BLE_SCAN_AGGRESSIVE_WINDOW;
      return;
    case BleScanIntensity::LIGHT:
      interval = BLE_SCAN_LIGHT_INTERVAL;
      window = BLE_SCAN_LIGHT_WINDOW;
      return;
    case BleScanIntensity::NORMAL:
    default:
      interval = BLE_SCAN_NORMAL_INTERVAL;
      window = BLE_SCAN_NORMAL_WINDOW;
      return;
  }
}

void applyLiveBleScanIntensity(BleScanIntensity intensity);
BleScanIntensity liveBleScanIntensity();

constexpr uint32_t BOOKOO_CONNECT_BEEP_DEFER_MS = 750;
constexpr uint32_t SCALE_WORKER_STALE_MS = 2000;
constexpr uint32_t SCALE_WORKER_NO_SCALE_DELAY_MS = 10;
constexpr uint32_t SCALE_WORKER_BACKGROUND_MS = 25;
constexpr uint32_t SCALE_LINK_SNAPSHOT_INTERVAL_MS = 50;
constexpr uint32_t SCALE_LINK_RSSI_SAMPLE_MS = 1000;
constexpr uint32_t SCALE_ATT_TIMEOUT_MS = 1000;
constexpr size_t SCALE_COMMAND_QUEUE_LENGTH = 12;
constexpr size_t SCALE_EVENT_QUEUE_LENGTH = 32;
constexpr uint32_t SCALE_WORKER_TASK_STACK_SIZE = 6656;
constexpr uint32_t BLE_STACK_READY_WAIT_MS = 4000;
constexpr size_t BLE_COMPANION_REQUEST_QUEUE_LENGTH = 8;
constexpr size_t BLE_COMPANION_RESULT_QUEUE_LENGTH = 8;
constexpr int SCALE_WORKER_TASK_CORE = 1;
constexpr uint32_t HEALTH_TELEMETRY_INTERVAL_MS = 5000;

void scaleWorkerLoadPreferred(const char *mac, const char *name,
                              const ScaleHistoryEntry *history);
bool scaleWorkerCopyPreferredIfDirty(char *mac, char *name,
                                     ScaleHistoryEntry *history);
void scaleWorkerClearPreferredDirty();
bool scaleWorkerTakeConnectedEdge();

// Control publishes only the worker-owned subset of RuntimeConfig. The worker
// never reads the orchestrator's mutable runtimeConfig object directly.
void publishScaleWorkerPolicy(const RuntimeConfig &config, bool controlReady);

ScaleLinkSnapshot getScaleLinkSnapshot();
void setScaleLinkState(ScaleLinkState state);
void markScaleWorkerProgress();
uint32_t scaleWorkerTickDelayMs();
void serviceScaleLinkRssi(uint32_t nowMs = millis());
bool enqueueScaleCommand(const ScaleCommand &command, bool toFront = false);
bool publishScaleEvent(const ScaleEvent &event, bool critical);
bool initializeScaleWorker();
void resetScaleWorkerRadioStateForHost();
bool scaleWorkerReady();
uint32_t scaleWorkerDroppedEventCount();
uint32_t scaleWorkerStackMinWordsValue();

#if defined(SHOT_STOPPER_HOST_TEST)
void setScaleWorkerBleReadyForHost(bool ready);
void setScaleWorkerTaskPresentForHost(bool present);
void setScaleWorkerStackMinWordsForHost(uint32_t words);
void resetScaleWorkerMetricsForHost();
#endif

void copyPreferredScaleMac(char *out, size_t capacity);
void copyPreferredScaleName(char *out, size_t capacity);
void copyScaleHistory(ScaleHistoryEntry *out);
bool hasPreferredScaleMac();
uint32_t scaleMacCachePauseRemainingMs(uint32_t nowMs);
bool scaleDiscoveryPaused(uint32_t nowMs = millis());
void noteScaleHistory(const char *mac, const char *name, bool persist);
void selectPreferredScale(const char *mac, const char *name);
void clearPreferredScaleCache();
void clearPreferredScaleSelectionOnly();
void requestScalePreferenceModeReset();

bool enqueueScaleDebugCommand(BookooDebugAction action, uint8_t beepLevel);
bool scaleHasVolumeControl();

void requestScaleBrewBeep(uint32_t cycleId);
void cancelScaleBrewBeep(uint32_t cycleId);
void requestScalePaddleReturnReminderBeep();
void cancelScalePaddleReturnReminderBeep();
void requestScaleCompletionBeep();
void cancelScaleCompletionBeepMailbox();
void cancelOperationalScaleBeeps();

// Orchestrator-owned. The worker task is the BLE radio guest for Companion.
extern EspressoScaleBLE scale;
extern QueueHandle_t scaleCommandQueue;
extern QueueHandle_t scaleEventQueue;
extern portMUX_TYPE scaleLinkMux;
extern portMUX_TYPE scalePreferredMacMux;
extern portMUX_TYPE scaleCriticalEventMux;
extern portMUX_TYPE scaleWeightEventMux;
extern uint32_t scalePacketSequence;
extern char scalePreferredMac[PREFERRED_SCALE_MAC_CAPACITY];
extern char scalePreferredName[PREFERRED_SCALE_NAME_CAPACITY];
extern ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY];
extern uint32_t scaleHistorySeq;
extern bool scalePreferredMacDirty;
extern uint32_t scaleDiscoveryPausedUntilMs;
extern ScaleEvent scaleCriticalEvent;
extern bool scaleCriticalEventPending;
extern ScaleEvent scaleTimerStartEvent;
extern bool scaleTimerStartEventPending;
extern ScaleEvent scaleWeightEvent;
extern bool scaleWeightEventPending;
extern bool scaleBeepPending;
extern uint32_t scaleBeepCycleId;
extern bool scaleCompletionBeepPending;

}  // namespace shotstopper
