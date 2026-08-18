# Emergency Recovery with Paddle

This procedure allows you to recover the Micra Shot Stopper when there is no
access via Web UI, Wi-Fi, BLE, or USB/serial. It does not require the scale to
be powered on or connected.

> **Safety:** During recovery, the firmware keeps CN9 open and does not allow
> starting an extraction or rinse. Do not attempt to make coffee until the
> procedure completes and the controller restarts.

## Choosing the Procedure

| Procedure | Gesture | Erases | Preserves |
| --- | --- | --- | --- |
| Recover access | `OFF→ON ×3` | Wi-Fi STA, static IP, last-known-good network, AP/Web UI password | Machine configuration, presets, calibration, scales, and history |
| Factory reset | `OFF→ON ×5` | All configuration, network, calibration, scales, BLE Companion, history, and last shot | Firmware only |

After either procedure, local access returns to:

- Network: **`MicraShotStopperAP`**
- AP and Web UI password: **`Micra1234`**
- Address: **`http://192.168.4.1`**

Passwords are case-sensitive.

## Before You Begin

One cycle means moving the paddle completely from **OFF to ON**. The
controller must initially power on with the paddle in **ON**; that initial
position does not count as a cycle.

- Perform all gesture movements in less than 5 seconds.
- After the last ON, do not move the paddle for 3 seconds.
- Recovery mode lasts 60 seconds total.
- If the firmware was compiled without a buzzer, the same steps work silently.

## Recover Wi-Fi, AP, and Password

This procedure does not erase recipes, machine settings, or history.

1. Power off the Shot Stopper.
2. Move the paddle to **ON**.
3. Power on the Shot Stopper while holding the paddle ON.
4. Wait for a continuous beep lasting 1.5 seconds that announces recovery mode.
5. Within 60 seconds, perform three complete cycles in less than 5 seconds:

   ```text
   Initial position: ON
   OFF → ON → OFF → ON → OFF → ON
        cycle 1   cycle 2   cycle 3
   ```

6. Keep the paddle still on ON for 3 seconds.
7. Three short beeps confirm that access credentials were restored.
8. Wait for the restart and connect to `MicraShotStopperAP` with `Micra1234`.

Example of valid timing:

```text
0.0 s  first OFF
0.5 s  first ON
1.0 s  second OFF
1.5 s  second ON
2.0 s  third OFF
2.5 s  third ON
5.5 s  static confirmation ends; access reset
```

## Perform a Factory Reset

> **Warning:** This procedure erases presets, configuration, learned
> calibration, networks, scales, BLE Companion, history, and last shot. It
> cannot be undone.

1. Power off the Shot Stopper.
2. Move the paddle to **ON**.
3. Power on the Shot Stopper while holding the paddle ON.
4. Wait for a continuous beep lasting 1.5 seconds.
5. Within 60 seconds, perform five complete cycles in less than 5 seconds:

   ```text
   Initial position: ON
   OFF → ON → OFF → ON → OFF → ON → OFF → ON → OFF → ON
        cycle 1   cycle 2   cycle 3   cycle 4   cycle 5
   ```

6. Keep the paddle still on ON for 3 seconds.
7. Five short beeps confirm the factory reset.
8. Wait for the restart and perform setup from `http://192.168.4.1`.

Example of valid timing:

```text
0.0 s  first OFF
0.4 s  first ON
0.8 s  second OFF
1.2 s  second ON
1.6 s  third OFF
2.0 s  third ON
2.4 s  fourth OFF
2.8 s  fourth ON
3.2 s  fifth OFF
3.6 s  fifth ON
6.6 s  static confirmation ends; factory reset
```

The first three cycles of the long gesture resemble the short gesture. There is
no risk of premature application: any movement restarts the confirmation wait,
and the firmware decides only after 3 seconds without motion.

## Cancel Without Erasing Data

Stop moving the paddle and allow the total 60-second window to expire. A beep
lasting 1.5 seconds announces the exit. Then move the paddle to OFF; the
firmware continues normal startup and CN9 stays open until it detects a stable
OFF.

You can also cut power before the 3-second confirmation ends. If the erase had
already begun, persistent intent will cause the next startup to complete the
operation safely.

## Common Mistakes

- **Starting with paddle OFF:** Starts normally; does not enter recovery mode.
- **Moving too slowly:** If cycles take more than 5 seconds, the attempt is
  invalidated. You can retry within the 60-second window.
- **Performing four cycles:** Does not correspond to any command and does not
  erase data.
- **Moving during the 3 seconds:** Restarts the confirmation or converts the
  short gesture into the long gesture if five cycles complete in time.
- **Exhausting the 60 seconds:** Recovery mode exits without executing a reset.
- **Not hearing beeps:** The build may not include a buzzer. Count movements
  and timings anyway.

## If It Does Not Restart or AP Does Not Appear

1. Wait at least 20 seconds after confirmation.
2. Verify that the paddle is OFF and power the controller back on.
3. Look for `MicraShotStopperAP`; the initial STA attempt may delay its
   appearance by approximately 15 seconds.
4. If you hear a long-short-long pattern and startup does not continue, there
   was a storage failure. CN9 stays open. Cut and restore power to
   automatically retry the pending operation.
5. If the issue persists, use the [USB CLI](SERIAL_CLI.md) or reflash the
   firmware before connecting CN9 again.

