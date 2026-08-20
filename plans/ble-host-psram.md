# Plan: host BLE en PSRAM (ArduinoBLE / VHCI)

## Objetivo

Liberar SRAM interna de **objetos del host ArduinoBLE** (descubrimiento GAP, árbol ATT remoto, perfil Companion) colocándolos en PSRAM, **sin** cambiar el stack pinado (ArduinoBLE 2.1.0 + controller-only) y **sin** retrasar de forma medible los informes de peso.

No es el objetivo meter el **controller** en PSRAM: en ESP32-S3 no hay flag para eso y el BSS/data del radio (~70 KiB) debe quedarse en DRAM interna.

## Por qué no basta con Kconfig

Este firmware habla con el radio por VHCI (`CONFIG_BT_CONTROLLER_ONLY` en IDF; `HCIVirtualTransport` en Arduino-CLI). No hay host Bluedroid ni NimBLE.

| Flag | Efecto real aquí |
| --- | --- |
| `CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST` | Solo Bluedroid. Con controller-only no se aplica. Activar Bluedroid pelearía con ArduinoBLE. |
| `CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY` | Igual: Bluedroid. |
| `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` | Solo NimBLE host. Migrar a NimBLE está fuera de alcance (`libraries/AcaiaArduinoBLE/README.md`). |
| `CONFIG_SPIRAM*` ya en `idf/sdkconfig.defaults` | Habilita el heap PSRAM. No redirige alocaciones BLE. |

La implementación es **código + parche ArduinoBLE**, no un `sdkconfig` extra.

## Qué se mueve y qué no

```
balanza ──aire──► controller (SRAM, ~70 KiB, no se toca)
                      │ VHCI
                      ▼
              rec/send StreamBuffer 258 B  ← SRAM (callback VHCI)
              bleTask 2 KiB stack          ← SRAM
                      │
                      ▼
              HCI._recvBuffer 258 B        ← SRAM (BSS)
              ATT / GAP / GATT new+malloc  ← PSRAM (este plan)
              ShotStopperBleCompanion      ← PSRAM con fallback
              scale_worker 5 KiB stack     ← SRAM
```

**Mover (host, solo desde el task que hace `BLE.poll()`):**

- Cola GAP de anuncios (`malloc(sizeof(BLEDevice))`, ya OOM-safe; hasta 32 dispositivos).
- Nodos `BLELinkedList` (mismo parche).
- `BLERemoteDevice` / services / characteristics / descriptors (`new` en `ATT.cpp`).
- Valores locales Companion (`BLELocalCharacteristic` hace `malloc(valueSize)` para SSID/password/IP).
- El objeto `ShotStopperBleCompanion` (hoy `allocInternal` en `shotStopper.cpp`).

**No mover:**

- Controller BSS/data, `esp_bt_controller_*`.
- `xStreamBufferCreate(258)` y `notify_host_recv` (contexto VHCI).
- Stack de `bleTask`, `scale_worker`, loop.
- `HCIClass::_recvBuffer` y buffers ATT de 64 B (`holdBuffer` / `writeBuffer`).
- Colas FreeRTOS Companion / scale events.

## Fases

### 0 — Baseline (sin cambio de comportamiento)

En n16r8 (y n8r4 si hay placa):

1. Boot + balanza conectada + Companion ON + Web UI polling.
2. Capturar `HEALTH` / `/api/v1/status/diagnostic`: `heapFree`, `heapLargest`, `minimumFreeHeap`, `psramFree`.
3. Telemetría de peso: intervalo entre `newWeightAvailable()` (p50/p95/max) y `lastValidPacketAgeMs` durante un shot o 2 min en idle.
4. Anotar tamaño de `sizeof(ShotStopperBleCompanion)` en un `static_assert` o log de boot (orden de magnitud: el objeto + 17 characteristics con buffers de valor).

Abortar el resto si el heap interno ya está holgado (no hay presión): el cambio no paga el riesgo.

### 1 — Allocator BLE host

En `ShotStopperPsram.h` (o un helper mínimo usado por el parche):

- `allocBleHost(size)` → `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`, fallback `INTERNAL | 8BIT`.
- `freeBleHost(ptr)` → `heap_caps_free`.

No usar `malloc()` genérico: con `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (IDF) / umbral Arduino, los bloques chicos de GAP/ATT **se quedan en SRAM**. El punto de este plan es saltar ese umbral.

El parche ArduinoBLE no puede depender de headers de Shot Stopper si se aplica a la copia de `~/Documents/Arduino/libraries/ArduinoBLE`. Opciones (elegir una):

- **A (preferida):** helper local en el parche (`#if defined(ESP32)` + `esp_heap_caps.h`), sin acoplar ArduinoBLE al firmware.
- **B:** wrapper en AcaiaArduinoBLE y no tocar ATT/GATT (cubre menos SRAM).

Usar A: el vehículo ya existe (`scripts/patch_arduinoble.sh` + `patches/`).

### 2 — Parche ArduinoBLE 2.1.0

Extender (no reemplazar) `patches/ArduinoBLE-2.1.0-oom-safe-discover.patch`:

| Sitio | Cambio |
| --- | --- |
| `GAP.cpp` BLEDevice | `malloc` → `heap_caps_malloc(SPIRAM\|8BIT)` + fallback |
| `BLELinkedList.h` nodos | igual |
| `ATT.cpp` `new BLERemote*` | placement `new` sobre el mismo allocator (mismo patrón OOM-safe: no `operator new`) |
| `GATT.cpp` servicios locales 1800/1801 | igual |
| `BLELocalCharacteristic.cpp` `_value` | `heap_caps_*` en vez de `malloc` |

`HCIVirtualTransport.cpp`: **no** tocar stream buffers ni `bleTask`.

Aplicar también en el clone IDF (`idf/third_party/ArduinoBLE`) desde `scripts/build-idf` / `ss_ensure_arduino_ble`, no solo en la librería Arduino-CLI.

### 3 — Companion en PSRAM

En `shotStopper.cpp` (~línea del `allocInternal(sizeof(ShotStopperBleCompanion))`):

```cpp
void *storage = allocExternalOrInternal(sizeof(ShotStopperBleCompanion));
```

El perfil GATT (characteristics, valores SSID/password) acaba en PSRAM de dos vías: el objeto y los `malloc` del parche.

Si un soak enseña cache-off panic en save NVS / OTA con Companion conectado, revertir **solo** este paso a `allocInternal` y dejar GAP/ATT remotos en PSRAM (la balanza no escribe flash en el hot path de peso).

### 4 — Telemetría y abort

Añadir (CLI o diagnostic, sin UI nueva):

- `bleHostAllocPsram` / fallback count (contador en el helper del parche, o log una vez al conectar).
- Intervalo de peso: reutilizar `lastValidPacketAgeMs`; opcionalmente p95 en HEALTH.

Abortar y revertir si:

- `BLE.begin()` fail / `ble=fail`.
- Edad de paquete p95 sube de forma clara vs baseline (p.ej. >20 ms extra sostenido, o drops durante shot).
- Panic cache-disabled en persist/OTA (M81).
- Scan idle pierde nombres / `takeSeenAdvertisement` se queda vacío bajo heap interno bajo (regresión del parche OOM).

### 5 — Pruebas

Reusar, no inventar una batería nueva:

- Host: tests AcaiaArduinoBLE existentes (el parche no debe romper stubs).
- M72 (BLE + Web UI + `WEBUI_RESTART` con balanza).
- M68 reducido si Companion está en el build (descubrir `0FFE` con escala conectada).
- M81 (persist + OTA + Wi-Fi scan; heap interno no debe stair-step; `psramFree` baja).
- Un shot BBW Bookoo: corte por peso igual que baseline (no “un tick tarde” sistemático).

Producción oficial es `./scripts/build-idf`. El pipeline arduino-cli
(`./scripts/build`) queda como legado no soportado; el parche ArduinoBLE
aplica a ambos.

## Impacto esperado

| Pieza | SRAM | Rendimiento peso |
| --- | --- | --- |
| Controller | ~70 KiB, igual | igual |
| GAP scan (32 × BLEDevice + listas) | baja (varios KiB en scan activo) | irrelevante con balanza ya conectada |
| ATT remoto (1 conexión) | baja (servicios/chars de la Acaia) | posible jitter µs–ms en `BLE.poll`; no cambia el intervalo de la balanza |
| Companion ON | la mayor ganancia host (objeto + 17 chars) | no está en el path de peso |
| StreamBuffer / bleTask | 0 | — |

No esperar decenas de KiB del controller. La ganancia es **host + Companion**, del orden de **unos pocos KiB a ~10–20 KiB** según Companion ON/OFF y scan. Medirlo en fase 0/4; si es &lt;4 KiB, no fusionar.

Riesgo de peso “más lento”: solo jitter de host bajo contención cache (Web UI + Wi-Fi + flash). El aire y el controller no cambian. Mitigación: no mover VHCI ni stacks; abortar con p95 de intervalo.

## Fuera de este plan

- Migrar AcaiaArduinoBLE a NimBLE / Bluedroid.
- `CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST` / `BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`.
- Stacks FreeRTOS en PSRAM (`CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`).
- Bajar `CONFIG_BT_CTRL_BLE_MAX_ACT` (828 B/instancia, SRAM del **controller**): útil como trabajo aparte si hace falta más DRAM sin tocar PSRAM.

## Orden de merge

1. Fase 0 (números en el PR o en el comentario de HEALTH).
2. Parche allocator + `patch_arduinoble.sh` / `build-idf`.
3. Companion `allocExternalOrInternal`.
4. Telemetría mínima + M72/M81/M68.
5. No mezclar con bump de core Arduino-ESP32 ni con cambio de ciclo de vida BLE.
