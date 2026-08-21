# Plan: first flow, retare y `minimumCupWeightG`

Plan único: issues abiertos, especificación (siempre el setting, nunca un peso duro), qué no tocar, y la batería de tests. Vasos de cartón ≥ el min configurado (default 10 g) deben retarear igual que 150 g.

## Checklist

- [x] Auditar: FSM, `stepFirstFlow`, latch y tests usan `runtimeConfig.minimumCupWeightG`; cero literales 10/150 en la regla
- [x] P1: residual FIRE solo si leftover &lt; 2 g y `peak - baseline` &lt; `cupMinG` (settle post-taza no pita)
- [x] Tests RT parametrizados por min vaso: 10.5/15/20 con min 10; 18 con min 15; 25 con min 20; sub-mínimo no retarea
- [x] Tests aterrizaje 12→15 y overshoot 18→15 con min 10; clasificador con `cupMinG` explícito
- [x] P2 opcional: `TIMER_START` combinado no rearma si vaso ya PRESENT (gracia cerrada) o retare hecho
- [x] Docs: umbral = setting min cup, no 150 g ni 10 g fijos

## Especificación (obligatoria)

El umbral de “esto es un vaso” es **solo** el setting `minimumCupWeightG` (`RuntimeConfig` en [`shotStopper/ShotStopperDomain.h`](../../shotStopper/ShotStopperDomain.h), default 10 g, rango validado **1–500 g**). Default 10 g existe porque los vasos de cartón pesan ~12–20 g; **no es un peso duro en el clasificador**.

Misma fuente en todos los sitios:

| Sitio | Debe leer |
| --- | --- |
| FSM `PLACED` | `runtimeConfig.minimumCupWeightG` en [`ShotStopperCupPresence.h`](../../shotStopper/ShotStopperCupPresence.h) (ya) |
| `stepFirstFlow(..., cupMinG)` | el mismo valor, pasado desde [`considerScaleFlowMarkers`](../../shotStopper/ShotStopperScaleSense.h) (ya) |
| Latch first flow | el mismo valor (ya) |
| Tests | `runtimeConfig.minimumCupWeightG` o argumento `cupMinG` explícito; **nunca** asumir 10 o 150 en la regla |

Reglas relativas a `cupMinG` (llámese M):

1. Peso estable **≥ M** → vaso → `PLACED` puede retarear. **Nunca** first drop.
2. First drop = café **&lt; M** y pasos **&lt; 2 g** (chorrito SEEKING), o leftover de dedo **&lt; 2 g y &lt; M** con pico también **&lt; M**.
3. Un gush de ~M gramos y un vaso de cartón de ~M gramos **no se distinguen**. Prioridad: retare, no first drop.
4. Cerámica 150 g y cartón 15 g son el mismo camino: ambos ≥ M.

`FIRST_DROP_DEFAULT_CUP_MIN_G = 10` solo si el argumento es inválido. No añadir literales 10/12/15/150 en la lógica de producción.

## Estado ya en código (no rehacer)

- SEEKING/TOUCH; salto ≥ 2 g = TOUCH (dedo o vaso).
- Residual leftover ≥ 2 g no FIRE (overshoot 160→150 o 18→15).
- Through exige peso **actual** `delta < cupMinG` (no un 10 fijo). A 15 g con M=10 esa rama no corre.
- Latch ignora TOUCH y masa ≥ `cupMinG`.
- `TIMER_START` no rearma gracia salvo combinado o gracia aún abierta.
- Historial: `stop_detail` nombra `paddle`, `web_stop`, `wall_limit`, etc. (`other` solo legacy).
- Tests 150 g: RT01, RT14 (overshoot 160→150), RT15 (creep 150.4). **No cubren cartón 12–20 g ni M ≠ 10.**

## Issues abiertos y remedio

### P1 — Residual FIRE tras pico de vaso (settle de tara)

TOUCH: caída ≥ 2 g del pico + leftover 0.3–2 g × 2 muestras = “dedo+café” (FF06). Si el pico era un vaso (150 o **15 g**), settle 15 → 1.2 → 1.0 **antes** de `markTareZeroReady` pita first drop y puede bloquear retare.

**Arreglo:** residual FIRE solo si leftover es café (`< 2 g` y `< cupMinG`) **y** `peakG - baseline < cupMinG`.

No-regresión: FF06 con M=10 (pico 8 &lt; 10) sigue FIRE. 15→1.2 con M=10 no. 8→1.2 con M=5 no (pico ≥ M, coherente con FSM). No usar leftover `< M` sin el tope de 2 g.

### P1 rechazado — Through FIRE a ~M g (gush 8→10.1)

Confundiría vaso de cartón (8→12→15) con café. Through **se queda** `delta < cupMinG`. Un gush que llega a ≥ M no tiene first drop; el stop por peso sigue. Aceptado.

### P2 — `TIMER_START` combinado tarde

Si `usedCombinedTareStart`, se rearma gracia aunque ya cerró: hold borra `PLACED`. **Arreglo opcional:** no rearmar si vaso ya PRESENT o `retarePerformed`. No quitar el rearm combinado del todo (Bookoo tara+start).

### P2 — TOUCH pegado sin retare (no FIRE)

Taza fuera de ventana / retare OFF → TOUCH a ≥ M, café encima no cuenta, `firstDropS = —`. Disparar first drop sobre ≥ M **es** el bug de retare. Solo documentar.

### P3 — M muy bajo (1–5 g) vs leftover de dedo

Si M=5, pico 8 g es vaso. Coherente. No bajar umbrales. La lógica sigue a M, no a 10.

## Qué no tocar

- Mutear first flow en gracia (FF08 chorrito).
- Through con +0.3 g (beep al poner taza).
- Residual leftover ≥ 2 g (overshoot de vaso).
- Latch en fase TOUCH a masa ≥ M.
- Reescribir filas de historial con `other`.

## Tests a añadir

Archivo: [`shotStopper/tests/shot_stopper_host_test.cpp`](../../shotStopper/tests/shot_stopper_host_test.cpp). Helper: fijar `runtimeConfig.minimumCupWeightG = M`, misma secuencia que RT01 (start, tara, gracia cerrada, ventana abierta).

### Retare (integración)

| M (setting) | Carga | Esperado |
| --- | --- | --- |
| 10 | 15 g y 20 g estables | retare, `firstDropMs == 0` |
| 10 | 10.5 g (justo sobre M) | retare, no first drop |
| 10 | 12 → 15 g (dos paquetes) | retare, no first drop |
| 10 | overshoot 18 → 15, 15.1, 15 | retare, no first drop |
| 10 | 7 g (RT02) | no retare |
| 15 | 18 g estable | retare, no first drop |
| 15 | 12 g estable | no retare |
| 20 | 25 g estable | retare, no first drop |
| 10 | 150 g (RT01/RT14/RT15) | siguen pasando |

### Clasificador `stepFirstFlow(..., 0, cupMinG)`

- 15 / 15.4 / 15.8 con M=10 → TOUCH, no FIRE
- 18 / 18.4 con M=15 → TOUCH, no FIRE
- 8, 12, 15 con M=10 → no FIRE
- 160, 150, 150.1 con M=10 → no FIRE (ya en FF01)
- FF06 8→1.2/1.3 con M=10 → FIRE
- 8→1.2/1.3 con M=15 → FIRE (pico y leftover &lt; 15)
- 8→1.2/1.3 con M=5 → no FIRE tras puerta de pico
- 15→1.2/1.3 con M=10 → no FIRE (pico ≥ M) — cubre P1 residual
- FF02/FF03/FF08/FF11, RT14/RT15 intactos

Regresión: host tests + ASan.

## Implementación (orden)

1. Puerta residual `peak < cupMinG` en [`ShotStopperScaleTypes.h`](../../shotStopper/ShotStopperScaleTypes.h).
2. Tests parametrizados por M (retare + clasificador).
3. Docs: [`tare-retare.md`](../features/tare-retare.md) / FAQ — umbral = setting, cartón ≥ M igual que cerámica.
4. P2 `TIMER_START` combinado solo con test de ack tardío + vaso PRESENT.

Archivos: [`ShotStopperScaleTypes.h`](../../shotStopper/ShotStopperScaleTypes.h), [`ShotStopperScaleSense.h`](../../shotStopper/ShotStopperScaleSense.h) (solo auditar que pasa el setting), [`shot_stopper_host_test.cpp`](../../shotStopper/tests/shot_stopper_host_test.cpp), docs de tara.
