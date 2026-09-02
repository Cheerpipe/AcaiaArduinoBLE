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
**Edit in YAML**. Paste the automation from section 3; saving it creates the
endpoint.

Replace the example webhook ID with a long random value known only to you.
Treat it like a password: anyone who knows it can send false readings.

```text
http://192.168.1.50:8123/api/webhook/shot_stopper_replace_with_a_long_secret
```

In Shot Stopper, open **Admin → Webhooks**, enable webhooks, paste that URL,
select the three events, and save. **Send test** checks the route without
changing the extraction sensors. A `webhook_id` can belong to only one Home
Assistant automation, so keep `local_only: true` when both devices are on the
same network.

## 2. Create entities to hold the readings

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

