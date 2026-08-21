# Domain separation: Machine / Scale / Brew / Orchestrator

Map of shot-control logic after extracting three encapsulated domains
coordinated by a thin orchestrator. This document does **not** replace
[`MACHINE_CONTROL_ABSTRACTION.md`](MACHINE_CONTROL_ABSTRACTION.md): that file
remains the plan for Momentary / Hall. This split is the prerequisite so
Momentary can swap the machine driver without touching brew or scale internals.

**Invariant:** paddle / Micra brew-by-weight semantics stay identical.
Host tests must stay green. No extra heap on the shot path: domain state is
BSS/static; the weight trajectory stays a small internal-RAM ring.

## Dependency rule

```
Machine ──┐
          ├──► ShotOrchestrator ◄── BrewPolicy
Scale ────┘         ▲
                    └── CycleContext / commands only
```

- `Machine` and `Scale` do not include each other.
- `Brew` decides shot policy. It does not touch GPIO or BLE.
- `ShotOrchestrator` (glue in `shotStopper.cpp`) is the only layer that
  wires events → decisions → actuation.

## Domains

### Machine — running / stopped + paddle + relay

| Owns | Does not own |
| ---- | ------------ |
| Paddle GPIO debounce, user intent edges | `PaddleMode` Natural / Original / Auto |
| CN9 / relay arm-commit, hard and operational timers | BBW, first drop, extraction guards |
| Relay-echo `MachineRunState` (ON/OFF) | Scale pairing / weight stream |
| Electrical safety trips (always allowed to open) | Shot `StopperState` |

API: `poll` intention, `machineRunState()`, `machineElapsedMs()`,
`machineRunningElapsed()` (one snapshot when callers need closed+elapsed),
`requestStart()` / `requestStop()` (gated; stop is a no-op if already open).

See [`ShotStopperMachine.h`](../shotStopper/ShotStopperMachine.h) and
[`ShotStopperMachineTypes.h`](../shotStopper/ShotStopperMachineTypes.h).

### Scale — measurement and derived signals

| Owns | Does not own |
| ---- | ------------ |
| Weight freshness, slew/range validation, trajectory ring | Whether a retare is *allowed* (BBW window) |
| First-drop detector, stable-cup detector, cup-removed detector | `EndReason`, start/stop of the machine |
| Least-squares cut prediction against an injected target | Fast/slow/A→M policy |
| BLE link/commands stay in the orchestrator transport | Beep / alert routing |

Scale emits **signals**. Brew interprets them with recipe settings.

See [`ShotStopperScaleSense.h`](../shotStopper/ShotStopperScaleSense.h) and
[`ShotStopperScaleTypes.h`](../shotStopper/ShotStopperScaleTypes.h).

### Brew — BBW, guards, recipe, paddle modes

| Owns | Does not own |
| ---- | ------------ |
| `StopperState` shot SM | GPIO / relay timers |
| BBW protection and retare *policy* | BLE writes |
| Fast / slow extraction, A→M, no-cup, no-scale | Electrical hard-limit ISR |
| `PaddleMode` hold override | Scale MAC cache |

See [`ShotStopperBrew.h`](../shotStopper/ShotStopperBrew.h) and
[`ShotStopperBrewTypes.h`](../shotStopper/ShotStopperBrewTypes.h).

### ShotOrchestrator

Lives in [`shotStopper.cpp`](../shotStopper/shotStopper.cpp): setup/loop,
web/serial bridges, alerts, shot log, BLE worker. It:

1. Polls Machine intention / run state.
2. Feeds Scale samples into detectors.
3. Asks Brew for the next command (`BrewCommand` + `EndReason`).
4. Executes start/stop, tare/timer, alerts.

`RuntimeConfig` / NVS remains one composed blob in
[`ShotStopperDomain.h`](../shotStopper/ShotStopperDomain.h) (schema unchanged).

## Persistence

NVS I/O, current-schema types, and migrations are separate:

- Infra: flash lock/scratch, `chooseNewerRevision`, Preferences include.
- Types: current shot-log schema and STA/LKG/AP field policy (no Preferences).
- Migrate: `decodeShotLogBlob` (v2–v6) and recipe→bank. No NVS.
- I/O: one header per store (settings, shot log, last-shot, BLE, recovery intent).
- Fachada: `resetAllDurableStores` (boot recovery and Web factory reset).

Machine / Scale / Brew do not include NVS I/O or `*Migrate.h`.

## Memory

- No `new` / `malloc` in domain modules. State is file-scope BSS.
- `ShotTrajectory` is 32 samples in internal RAM (prediction window is 10).
  Do not move it to PSRAM: the weight path is latency-sensitive and must not
  fragment or wait on the external bus.
- BLE / web / shot-log buffers keep using `allocExternalOrInternal()` (PSRAM
  first) as they already do.

## Files

| File | Role |
| ---- | ---- |
| `ShotStopperHardware.h` | Board pins, paddle/relay levels |
| `ShotStopperMachineTypes.h` / `ShotStopperMachine.h` | Machine types + driver |
| `ShotStopperScaleTypes.h` / `ShotStopperScaleSense.h` | Scale types + detectors |
| `ShotStopperBrewTypes.h` / `ShotStopperBrew.h` | Brew types + policy |
| `ShotStopperDomain.h` | Compose + NVS/UI/debug leftovers |
| `ShotStopperShotLogTypes.h` | Shot log schema 7 + domain helpers |
| `ShotStopperShotLogMigrate.h` | Legacy v2–v6 + `decodeShotLogBlob` |
| `ShotStopperShotLog.h` / `ShotStopperLastShot.h` | Shot log / last-shot NVS I/O |
| `ShotStopperPersistence.h` | Settings blob NVS (dual-slot) |
| `ShotStopperDurableStores.h` | Factory reset of every durable store |

Implementation headers are included from `shotStopper.cpp` (single translation
unit) so host tests that `#include` the firmware keep seeing the same BSS
symbols (`paddleOn`, `cn9Closed`, `session`) without extra heap or ODR splits.
