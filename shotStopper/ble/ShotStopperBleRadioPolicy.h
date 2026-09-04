#pragma once

namespace shotstopper {

struct BleRadioPolicyInputs {
  bool scaleConnecting = false;
  bool scaleLinked = false;
  bool machineCircuitClosed = false;
  bool scaleHuntRfClear = false;
};

inline bool bleRadioPolicyPauseCompanionAdvertising(
    const BleRadioPolicyInputs &inputs) {
  return inputs.scaleConnecting || inputs.scaleLinked ||
         inputs.machineCircuitClosed || inputs.scaleHuntRfClear;
}

}  // namespace shotstopper
