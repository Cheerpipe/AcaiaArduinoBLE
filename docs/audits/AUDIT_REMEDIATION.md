# Audit remediation — BLE continuity

Shot Stopper pins ArduinoBLE **2.1.0** and applies four patches via
`scripts/patch_arduinoble.sh`:

1. GAP scan active **40/20 ms** (50% duty)
2. OOM-safe discovery (no `abort` on `bad_alloc`)
3. BLE host objects in PSRAM (`BLEHostAlloc`)
4. **HCI bounded waits** (`ArduinoBLE-2.1.0-hci-bounded-waits.patch`)

## HCI bounded waits (C1)

Stock ArduinoBLE ESP32 VHCI / host paths could block indefinitely:

| Site | Stock behavior | Patch |
|------|----------------|-------|
| `HCIVirtualTransport::notify_host_recv` | `portMAX_DELAY` | 1 s `pdMS_TO_TICKS`; drop on timeout |
| `bleTask` VHCI send-available spin | unbounded busy-wait | 1 s + `vTaskDelay(1)`; skip send on timeout |
| `HCIVirtualTransport::read` / `write` | `portMAX_DELAY` | 1 s |
| `HCIClass::sendAclPkt` credit wait | unbounded `poll()` loop | 1 s → return `-1` |
| ATT indication confirm | unbounded `while (!_cnf)` | bounded by `ATT::_timeout` |

`BLE.setTimeout(1000)` (Shot Stopper / AcaiaArduinoBLE) still bounds public ATT
request/response waits. Together with the HCI patch, every known host-side wait
on the ESP32 VHCI path has a hard deadline ≤ ~1 s (indication confirm uses the
configured ATT timeout).

## Residual risk

- A single ATT or HCI op can still stall the owner task for up to the configured
  timeout (~1 s). Shot Stopper mitigates TWDT pressure by **stepped GATT connect**
  and mid-command `BLE.poll()` + `esp_task_wdt_reset` between scale writes.
- Controller/radio soft-locks outside the host are not covered; the 60 s idle
  GAP restart (advert-gated) and packet silence disconnect remain the recovery
  path.
- Hardware soak **M72 / M73** remains required before release after ArduinoBLE
  or core bumps.

## Safety posture

CN9 fail-open on panic, task WDT, and the independent GPTimer is unchanged.
BLE remediation aims to prevent mid-shot TWDT panics and reconnect storms, not
to replace those hardware safety paths.
