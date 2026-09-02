# Webhooks with Home Assistant

Webhooks let Shot Stopper tell Home Assistant what is happening during an
extraction: when it starts, when the first drops appear, and how it finished.
They are notifications sent when something happens, not a permanent connection
or a way to start the machine remotely.

> **Before you begin.** The firmware sends webhooks only over `http://`, not
> `https://`. Use Home Assistant's local HTTP address for the ESP32, for
> example `http://192.168.1.50:8123`, and reserve that address in your router.

## 1. Create the receiving endpoint

In Home Assistant, go to **Settings → Automations & scenes → Create
automation → Create new automation**. Open the three-dot menu and choose
**Edit in YAML**. Paste the automation from section 2; saving it creates the
endpoint.

Replace the example webhook ID with a long random value known only to you.
Treat it like a password: anyone who knows it can send false readings.

```text
http://192.168.1.50:8123/api/webhook/shot_stopper_replace_with_a_long_secret
```

## 2. Receive, transform, and save each notification

Paste this into the automation from step 1 and change only `webhook_id`. The
templates read `trigger.json` and convert milliseconds to seconds.

```yaml
alias: Shot Stopper — receive extraction
description: Store Shot Stopper webhook events in helpers.
mode: queued
max: 10
triggers:
  - trigger: webhook
    webhook_id: shot_stopper_replace_with_a_long_secret
    allowed_methods:
      - POST
    local_only: true
variables:
  payload: "{{ trigger.json }}"
actions:
  - choose:
      - conditions:
          - condition: template
            value_template: "{{ payload.event == 'brew_state' }}"
        sequence:
          - action: input_text.set_value
            target:
              entity_id: input_text.shot_stopper_state
            data:
              value: "{{ payload.state }}"
          - action: input_number.set_value
            target:
              entity_id: input_number.shot_stopper_target_weight
            data:
              value: "{{ payload.targetWeightG | float(0) }}"
          - if:
              - condition: template
                value_template: "{{ payload.state == 'idle' }}"
            then:
              - action: input_number.set_value
                target:
                  entity_id: input_number.shot_stopper_duration
                data:
                  value: "{{ payload.durationMs | float(0) / 1000 }}"
              - action: input_text.set_value
                target:
                  entity_id: input_text.shot_stopper_stop_detail
                data:
                  value: "{{ payload.stopDetail }}"
      - conditions:
          - condition: template
            value_template: "{{ payload.event == 'first_drop' }}"
        sequence:
          - action: input_number.set_value
            target:
              entity_id: input_number.shot_stopper_first_drop
            data:
              value: "{{ payload.firstDropMs | float(0) / 1000 }}"
          - action: input_number.set_value
            target:
              entity_id: input_number.shot_stopper_target_weight
            data:
              value: "{{ payload.targetWeightG | float(0) }}"
      - conditions:
          - condition: template
            value_template: "{{ payload.event == 'end' }}"
        sequence:
          - action: input_number.set_value
            target:
              entity_id: input_number.shot_stopper_duration
            data:
              value: "{{ payload.durationMs | float(0) / 1000 }}"
          - action: input_number.set_value
            target:
              entity_id: input_number.shot_stopper_target_weight
            data:
              value: "{{ payload.targetWeightG | float(0) }}"
          - action: input_text.set_value
            target:
              entity_id: input_text.shot_stopper_shot_type
            data:
              value: "{{ payload.shotType }}"
          - action: input_text.set_value
            target:
              entity_id: input_text.shot_stopper_stop_detail
            data:
              value: "{{ payload.stopDetail }}"
          - if:
              - condition: template
                value_template: "{{ payload.weightG is defined }}"
            then:
              - action: input_number.set_value
                target:
                  entity_id: input_number.shot_stopper_final_weight
                data:
                  value: "{{ payload.weightG | float(0) }}"
          - if:
              - condition: template
                value_template: "{{ payload.firstDropMs is defined }}"
            then:
              - action: input_number.set_value
                target:
                  entity_id: input_number.shot_stopper_first_drop
                data:
                  value: "{{ payload.firstDropMs | float(0) / 1000 }}"
          - if:
              - condition: template
                value_template: "{{ payload.averageFlowGps is defined }}"
            then:
              - action: input_number.set_value
                target:
                  entity_id: input_number.shot_stopper_average_flow
                data:
                  value: "{{ payload.averageFlowGps | float(0) }}"
```

The `end` event arrives after Shot Stopper's drip delay, so it is the best
final result. Weight, first-drop time, and average flow can be omitted when no
reliable reading exists; the conditional actions leave the previous value
untouched instead of replacing it with zero.

## Event payloads

Every message is a `POST` with `Content-Type: application/json`.

| Field | Description |
| --- | --- |
| `schemaVersion` | Format version, currently `1`. |
| `event` | `brew_state`, `first_drop`, `end`, or `test`. |
| `deviceId` | Wi-Fi MAC, such as `AA:BB:CC:DD:EE:FF`. |
| `cycleId` | Numeric extraction ID; use it to relate events. |
| `uptimeMs` | Event time in milliseconds since boot. |
| `timestamp` | Unix/UTC seconds; `0` means the clock is not synced. |
| `sentAtUptimeMs` | Time the message was prepared, in milliseconds since boot. |

### `brew_state`

Normal extraction sends `brewing` at the start and `idle` at the end.

```json
{"schemaVersion":1,"event":"brew_state","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":912345,"timestamp":1767225600,"sentAtUptimeMs":912680,"state":"idle","durationMs":27800,"targetWeightG":36.0,"presetId":1,"stopDetail":"normal_target"}
```

| Field | Description | Domain |
| --- | --- | --- |
| `state` | Current state. | `brewing`, `idle` |
| `durationMs` | Duration in ms; `0` while brewing. | Number ≥ 0 |
| `targetWeightG` | Recipe target in grams. | Number ≥ 0 |
| `presetId` | Active recipe ID. | Integer ≥ 0 |
| `stopDetail` | End reason; empty while brewing. | See dictionary below |

### `first_drop`

```json
{"schemaVersion":1,"event":"first_drop","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":895000,"timestamp":1767225583,"sentAtUptimeMs":895040,"firstDropMs":4480,"weightG":1.24,"targetWeightG":36.0,"presetId":1}
```

| Field | Description |
| --- | --- |
| `firstDropMs` | Time from start to first drops, in ms. |
| `weightG` | Scale reading at first drops, in grams. |
| `targetWeightG` / `presetId` | Active recipe target and ID. |

### `end`

```json
{"schemaVersion":1,"event":"end","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":42,"uptimeMs":923400,"timestamp":1767225611,"sentAtUptimeMs":923480,"durationMs":27800,"targetWeightG":36.0,"presetId":1,"shotType":"auto","stopDetail":"normal_target","firstDropMs":4480,"weightG":36.72,"averageFlowGps":1.57}
```

| Field | Description | Domain |
| --- | --- | --- |
| `durationMs`, `targetWeightG`, `presetId` | Total time, target grams, and recipe ID. | Numbers ≥ 0 |
| `shotType` | How the shot was performed. | `auto`, `timer_only`, `manual` |
| `stopDetail` | Specific end reason. | See dictionary below |
| `firstDropMs`, `weightG`, `averageFlowGps` | First-drop time, final grams, and average g/s. | Optional decimal values |

### `test`

The **Send test** button sends the common fields with `event: "test"` and no
extraction measurements. It is useful for checking connectivity.

```json
{"schemaVersion":1,"event":"test","deviceId":"AA:BB:CC:DD:EE:FF","cycleId":0,"uptimeMs":930000,"timestamp":1767225618,"sentAtUptimeMs":930010}
```

## `stopDetail` dictionary

| Value | Meaning |
| --- | --- |
| `normal_target` | Reached the target weight normally. |
| `extended_max_weight` / `extended_min_time` | Fast-extraction guard extended weight or time. |
| `auto_to_manual` | A→M guard ended it after scale loss. |
| `slow_max_time` / `slow_min_weight` | Slow-extraction guard reached its time or weight boundary. |
| `cup_removed` | The cup was detected as removed. |
| `activator` | Configured physical activator ended it. |
| `web_stop` / `web_heartbeat` | Web stop requested, or web heartbeat timed out. |
| `physical_override` | Physical override was applied. |
| `hard_limit` / `wall_limit` | Firmware hard limit, or configured time limit. |
| `relay_safety` | Relay safety protection activated. |
| `weight_anomaly` | Weight anomaly detected. |
| `other` | No more specific reason. |
| `prediction` | Legacy value; not generated by new extractions. |

## If nothing arrives

Press **Send test** and check Webhooks status on the Admin screen. Confirm that
the URL starts with `http://`, contains the exact same secret, and is reachable
from the ESP32. Notifications run in the background and are not retried, so a
slow network cannot interfere with machine control or the scale. Use them for
logging and display, not as a safety mechanism or proof that every message
arrived.

For more information, see Home Assistant's official [webhook
trigger](https://www.home-assistant.io/docs/automation/trigger/#webhook-trigger)
and [Template sensors](https://www.home-assistant.io/integrations/template/)
documentation.

In Shot Stopper, open **Admin → Webhooks**, enable webhooks, paste that URL,
select the three events, and save. **Send test** checks the route without
changing the extraction sensors. A `webhook_id` can belong to only one Home
Assistant automation, so keep `local_only: true` when both devices are on the
same network.

## Additional entity definitions

Add these helpers to `configuration.yaml` (or a package), then restart Home
Assistant or reload the relevant YAML configuration.

```yaml
input_number:
  shot_stopper_duration:
    name: Shot Stopper duration
    min: 0
    max: 60
    step: 0.1
    unit_of_measurement: s
  shot_stopper_final_weight:
    name: Shot Stopper final weight
    min: 0
    max: 200
    step: 0.01
    unit_of_measurement: g
  shot_stopper_target_weight:
    name: Shot Stopper target weight
    min: 0
    max: 200
    step: 0.01
    unit_of_measurement: g
  shot_stopper_average_flow:
    name: Shot Stopper average flow
    min: 0
    max: 20
    step: 0.01
    unit_of_measurement: g/s
  shot_stopper_first_drop:
    name: Shot Stopper first drop
    min: 0
    max: 60
    step: 0.1
    unit_of_measurement: s
input_text:
  shot_stopper_state:
    name: Shot Stopper state
    max: 32
  shot_stopper_shot_type:
    name: Shot Stopper shot type
    max: 32
  shot_stopper_stop_detail:
    name: Shot Stopper stop detail
    max: 64
```

If you want entities in the `sensor` domain for dashboards and graphs, add:

```yaml
template:
  - sensor:
      - name: Shot Stopper last extraction duration
        unique_id: shot_stopper_last_extraction_duration
        state: "{{ states('input_number.shot_stopper_duration') }}"
        unit_of_measurement: s
        device_class: duration
        state_class: measurement
      - name: Shot Stopper last extraction final weight
        unique_id: shot_stopper_last_extraction_final_weight
        state: "{{ states('input_number.shot_stopper_final_weight') }}"
        unit_of_measurement: g
        device_class: weight
        state_class: measurement
      - name: Shot Stopper target weight
        unique_id: shot_stopper_target_weight
        state: "{{ states('input_number.shot_stopper_target_weight') }}"
        unit_of_measurement: g
        device_class: weight
        state_class: measurement
      - name: Shot Stopper average flow
        unique_id: shot_stopper_average_flow
        state: "{{ states('input_number.shot_stopper_average_flow') }}"
        unit_of_measurement: g/s
        state_class: measurement
      - name: Shot Stopper first drop
        unique_id: shot_stopper_first_drop
        state: "{{ states('input_number.shot_stopper_first_drop') }}"
        unit_of_measurement: s
        device_class: duration
        state_class: measurement
      - name: Shot Stopper extraction state
        unique_id: shot_stopper_extraction_state
        state: "{{ states('input_text.shot_stopper_state') }}"
      - name: Shot Stopper stop detail
        unique_id: shot_stopper_stop_detail
        state: "{{ states('input_text.shot_stopper_stop_detail') }}"
```
