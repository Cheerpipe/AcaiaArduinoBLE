# Informe de corrección — WebUI lazy HTML/JS split

Fecha: 2026-08-20  
Alcance: cambios de SPA lazy (`app.js`, `web/js/*`, `web/html/*`, `gen_web_ui.js`, `ShotStopperNetwork.*`, tests).  
Método: revisión estática del código y de paths de interacción (router, ownership, status/polls, saves).

## Resumen

La arquitectura lazy (shell + partials + módulos) es coherente y los tests de assets pasan, pero el split **expone DOM que ya no está montado** en varios caminos que antes asumían el HTML monolítico. Hay al menos **2 defectos P1** que pueden romper el poll de Home / el guardado de Settings, más un **P1 de timers duplicados** en Diagnostic/History.

No se modificó código en esta pasada; este documento es el backlog de corrección.

---

## Hallazgos

### [P1] `ingestPresets` crashea si Settings no está cargado — `shotStopper/web/js/runtime.js` (~L138)

Al cambiar el preset activo desde un poll de **Home**, se ejecuta:

```js
$('brewDirtyHint').classList.add('hidden')
```

`brewDirtyHint` vive en el partial de Settings. En Home cold-load ese nodo no existe → `TypeError` → se corta `applyHomeStatus` / el ciclo de status.

**Corrección:** usar el mismo patrón que `applyPreset`:

```js
if ($('brewDirtyHint')) $('brewDirtyHint').classList.add('hidden');
```

**Test sugerido:** en `check_web_assets.js`, assert de que ese acceso está guardado; o smoke: Home sin haber abierto Settings + cambio de `presets.activeId` en status.

---

### [P1] `saveMachineConfig` asume hints de Admin y Settings montados — `runtime.js` (~L135)

Tras un save exitoso:

```js
$('dateTimeDirtyHint').classList.add('hidden');
$('configDirtyHint').classList.add('hidden');
```

- Save desde **Settings** (sin haber visitado Admin): `dateTimeDirtyHint` es `null` → crash (el comando ya se envió; la UI queda inconsistente).
- Antes del split ambos nodos existían siempre en el DOM.

`ensureSettingsHydrated()` solo garantiza el partial de Settings, no Admin.

**Corrección:**

```js
if ($('dateTimeDirtyHint')) $('dateTimeDirtyHint').classList.add('hidden');
if ($('configDirtyHint')) $('configDirtyHint').classList.add('hidden');
```

---

### [P1] Timers de Log/History se duplican al re-entrar la misma vista — `shotStopper/web/app.js` (`startView` / `renderRoute`)

`startView` crea `logTimer` / `shotsTimer` **sin** limpiar los anteriores. `armStatusTimer` sí hace `clearInterval` del status timer.

Path:

1. Estar en Diagnostic (o History).
2. Pulsar de nuevo el mismo link de nav (o `renderRoute` con `view === activeView`).
3. Se llama `startView` otra vez → segundo `setInterval`.

Efecto: polls `/api/v1/log` o `/api/v1/shots` a ritmo doble (presión en el ESP32 / claim).

`claimWebUiOwnership` llama `stopViewPolls()` antes y no dispara este bug; el re-click de nav sí.

**Corrección:** al inicio de `startView`:

```js
clearInterval(logTimer); clearInterval(shotsTimer); logTimer = shotsTimer = 0;
```

o llamar `stopExtraPolls()` antes de rearmar.

---

### [P2] Race en `ensureView` si hay cargas concurrentes — `app.js` (~L45–57)

`htmlLoaded` / `jsMods` se actualizan **después** del `await`. Dos llamadas paralelas (p.ej. `ensureSettingsDom` desde flush de Home + navegación a Settings) pueden inyectar el partial dos veces.

Hoy `init()` tiene `ready`, así que los bindings no se duplican, pero el DOM se reescribe (pérdida de estado de form / focus).

**Corrección:** mapa de `Promise` in-flight por vista (`loading.get(name) || startLoad()`).

---

### [P2] Listener `window.load` huérfano para AP password — `runtime.js` (~L80)

Quedó el `addEventListener('load', … changeApPasswordButton)`. En el boot el botón Admin aún no existe; el listener no-op. Admin `init` vuelve a bindear.

No rompe, pero es código muerto confuso y puede reaparecer como bug si alguien asume que el load handler alcanza.

**Corrección:** eliminar el bloque `window.addEventListener('load', …)` (Admin `init` ya cubre el caso).

---

### [P3] Doble `'use strict';` al inicio de `runtime.js`

El preámbulo de hooks se concatenó delante del archivo original y dejó un segundo `'use strict';`. Sin impacto runtime; limpiar en el mismo PR de fixes.

---

## No hallazgos (o preexistente)

| Tema | Nota |
|------|------|
| Módulos `app.js` vs `./runtime.js?v=` | Misma URL absoluta `/js/runtime.js?v=…` → una sola instancia de módulo. |
| Ownership + cold load | `loadStatus` no-op hasta `claimWebUiOwnership`; el `finally` del boot está bien. |
| `#message` ausente en HTML | CSS/JS lo usan; el HTML reciente tampoco lo tenía (preexistente). `message()` ya guarda null; `noteReachOk` no — preexistente. Recomendable añadir `<p id="message" …>` al **shell** como mejora aparte. |
| Handlers HTTP / budgets | 51 rutas registradas, `max_uri_handlers=56`; gzip + ETag + immutable OK. |
| Flash combined ~39 KiB | Dentro del budget de test (44 KiB). |

---

## Plan de corrección sugerido (orden)

1. Guards en `ingestPresets` y `saveMachineConfig` (P1, trivial).
2. `startView` limpia `logTimer`/`shotsTimer` antes de armar (P1).
3. Deduplicar `ensureView` con promesa in-flight (P2).
4. Borrar listener `load` de AP password + limpiar `'use strict'` (P2/P3).
5. (Opcional) Añadir `#message` al shell y null-check en `noteReachOk`.

## Verificación post-fix

- `npm run gen:web-ui && node shotStopper/tests/check_web_assets.js`
- Manual: Home sin abrir Settings → forzar cambio de preset activo en status (o simular) → no debe romper polls.
- Manual: Settings → Save machine settings sin haber abierto Admin → no exception en consola.
- Manual: Diagnostic → click otra vez Diagnostic → en Network, `/api/v1/log` no debe doblar frecuencia.
- Manual: Home toggle cup/fast guard (flush preset) → debe cargar Settings en background y guardar sin navegar.
