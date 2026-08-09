# Status indicators

The firmware uses two independent one-pixel WS2812B status indicators. One
reports scale health and the other reports stopper workflow and safety. This
separation avoids the ambiguity of the legacy single RGB LED: scale health and
stopper state remain visible at the same time.

The indicators are diagnostic only. They do not authorize CN9 closure, replace
the Web UI safety panel, or replace electrical verification.

## Hardware type and board compatibility

The legacy firmware did not drive a WS2812B. It drove a three-channel,
common-anode RGB LED with inverted PWM (`255 - channel`) and required one GPIO
per color:

| Legacy target | Red | Blue | Green | LED type |
| --- | ---: | ---: | ---: | --- |
| Mazer ESP32-S3 V3 mapping | GPIO 46 | GPIO 45 | GPIO 47 | Common-anode discrete RGB |
| Mazer ESP32-C3 mapping | GPIO 21 | GPIO 10 | GPIO 20 | Common-anode discrete RGB |
| Arduino Nano ESP32 | Board `LED_RED` | Board `LED_BLUE` | Board `LED_GREEN` | Built-in common-anode RGB |
| Generic ESP32 mapping | GPIO 25 | GPIO 32 | GPIO 33 | External discrete RGB |

Those mappings describe the boards targeted by the original Shot Stopper
sketch. They are not features supplied by every ESP32-S3. The ESP32-S3 is the
microcontroller; the development-board designer decides whether to add an RGB
LED and which GPIO to use.

For example, Espressif documents an addressable RGB LED on GPIO 48 on the
[initial ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html),
but on GPIO 38 on
[ESP32-S3-DevKitC-1 v1.1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
Espressif notes that both revisions are on the market. This project's default
S3 relay output is GPIO 38, so using the v1.1 onboard RGB LED would conflict
with CN9 relay control and is rejected if selected as an LED pin.

The new design requires two WS2812B-compatible pixels, each on its own data
GPIO. Only a board whose onboard pixel is compatible and on a conflict-free pin
can supply one of them; otherwise both must be external. The Arduino Nano
ESP32's built-in discrete RGB LED cannot supply either pixel.

## Legacy single-LED behavior

Immediately before the two-LED implementation, the single RGB LED encoded the
following control states:

| Stopper state | Legacy indication |
| --- | --- |
| `READY`, scale available | Solid green |
| `READY`, scale unavailable | Solid red |
| `REQUIRES_OFF`, scale available | Solid yellow |
| `REQUIRES_OFF`, scale unavailable | Solid red |
| `QUALIFYING_ON` | Solid yellow |
| `BREW` | Alternating green and blue every second |
| `RINSE` | Solid cyan |
| `MANUAL_NO_SCALE` | Solid magenta |

The legacy mapping had no independent indication for BLE initialization,
scale-worker failure or staleness, safety `BOOT_SAFE`, `ARMING`, `TRIPPED`, or
`LOCKOUT`, watchdog failure, maintenance, timer-only operation, or a scale
disconnect that occurred during a workflow. Some of these conditions could
change the control state and therefore indirectly change the color, but they
did not have a unique visual code.

## Current scale LED

| Priority | Color and pattern | Condition |
| ---: | --- | --- |
| 1 | Slow blue blink | Firmware initialization is still in progress |
| 2 | Fast red blink | BLE subsystem or scale worker unavailable |
| 3 | Solid red | Scale disconnected |
| 4 | Slow yellow blink | Link is connected but the worker has stopped making timely progress |
| 4 | Solid green | Scale connected and worker responsive |

## Current stopper LED

Safety indications have priority over workflow indications:

| Priority | Color and pattern | Condition |
| ---: | --- | --- |
| 1 | Slow blue blink | `BOOT_SAFE` initialization |
| 2 | Fast red blink | Safety `TRIPPED` or `LOCKOUT`, watchdog fault, or required safety service unavailable |
| 3 | Slow blue blink | Maintenance reservation active |
| 4 | Slow amber blink | `REQUIRES_OFF`; return the physical paddle to OFF |

When none of those overrides applies, the pattern identifies the workflow and
the color family identifies the operating mode:

| Workflow | Automatic with scale | Manual, timer-only, or scale-lost |
| --- | --- | --- |
| `READY` | Solid green | Solid salmon |
| `QUALIFYING_ON` | Medium green blink | Medium salmon blink |
| `BREW` | Slow green blink | Slow salmon blink |
| `RINSE` | Fast green blink | Fast salmon blink |
| `MANUAL_NO_SCALE` | Not applicable | Slow salmon blink |

Blink phases are deterministic and use a 50% duty cycle:

| Pattern | ON | OFF | Complete cycle |
| --- | ---: | ---: | ---: |
| Slow | 750 ms | 750 ms | 1,500 ms |
| Medium | 300 ms | 300 ms | 600 ms |
| Fast | 125 ms | 125 ms | 250 ms |

## Reading both indicators

| Scale LED | Stopper LED | Interpretation |
| --- | --- | --- |
| Solid green | Solid green | Scale healthy; automatic mode ready |
| Solid green | Slow green blink | Scale healthy; automatic brew active |
| Solid green | Fast green blink | Scale healthy; rinse active |
| Solid green | Solid salmon | Scale healthy but timer-only mode selected |
| Solid red | Solid salmon | No scale; manual operation ready |
| Solid red | Slow salmon blink | No-scale manual brew active |
| Any | Slow amber blink | Physical paddle must return to OFF |
| Any | Fast red blink | Stopper safety fault; inspect Web UI/Serial diagnostics and do not rely on color alone |

## Configuration and implementation boundary

The defaults are listed in the root README. Override them with
`SHOT_STOPPER_SCALE_LED_GPIO`, `SHOT_STOPPER_STOPPER_LED_GPIO`, and
`SHOT_STOPPER_LED_BRIGHTNESS` from 1 through 255. Compile-time assertions reject
duplicate, non-output, paddle, relay, heartbeat, and feedback conflicts.

The control loop renders desired states into a one-item overwrite mailbox. A
low-priority indicator task owns the ESP32 core's WS2812-compatible output
function. This prevents a delayed LED transmission from blocking the safety
loop and ensures that only the newest visual state is retained. Failure to
initialize the indicator queue or task leaves control active and reports the
indicators as unavailable; it never relaxes CN9 safety.
