# Preguntas frecuentes (FAQ)

Comportamientos automáticos, límites de seguridad y dudas habituales al usar **Micra Shot Stopper**. Los valores por defecto corresponden a un firmware recién flasheado o tras **factory reset**.

Para detalle técnico completo, consulta el [README principal](../README.md).

## Comportamiento del shot y la balanza

| Pregunta | Respuesta |
| --- | --- |
| **No puedo hacer shots de más de 60 s. ¿Por qué?** | Existe un **límite duro de 60 s** en el firmware: ninguna ruta que cierre CN9 puede superar ese tiempo, aunque configures un valor mayor en la Web UI. Es una defensa de seguridad independiente del límite operativo (*CN9 limit*). |
| **Mi shot no dura más de ~50 s (u otro valor &lt; 60 s).** | Revisa **Configuration → CN9 limit (s)** en la Web UI. Ese parámetro define el tiempo máximo de CN9 cerrado **por ciclo** (rango 5–60 s; **por defecto 60 s**). Si lo tienes en 50 s, el shot se cortará ahí aunque el peso objetivo no se haya alcanzado. También puede influir el **A→M time guard** si la balanza se desconecta durante un shot automático (ver abajo). |
| **A veces mi shot termina antes o después del peso objetivo y la balanza no se detiene el flujo.** | Puede deberse al **Auto-to-manual time guard** (*A→M time guard*), activado **por defecto**. Si pierdes la balanza durante un shot automático (BLE caído o stream obsoleto), el firmware **sigue intentando reconectar** durante todo el ciclo. Mientras el control por peso esté suspendido, el guard puede **cerrar CN9** en un plazo fijo desde el inicio del ciclo (modo **Auto** = tendencia de los últimos 5 shots buenos, por defecto ~30 s; modo **Manual** = límite configurado, default 30 s). Si la balanza vuelve a tiempo, el countdown A→M se apaga y vuelven BBW / Fast extraction guard. Revisa el panel en vivo: aparece `A→M · …s` cuando está enforced. |
| **A veces el shot termina con 2–4 g de más respecto al objetivo.** | Si activaste el **Fast extraction guard**, es el comportamiento esperado cuando el peso objetivo se alcanza **demasiado pronto** (antes del tiempo mínimo de extracción). El shot entra en modo *extended* y puede seguir hasta el **peso máximo de recuperación** (p. ej. 42,5 g con objetivo 36 g) o hasta cumplir el **tiempo mínimo de brew** (p. ej. 26 s). **Está desactivado por defecto.** |
| **El shot llega al peso objetivo pero no se detiene.** | Durante los primeros segundos aplica la **confirmación de inicio de brew** (*Brew start confirmation*): ventana de protección contra paradas accidentales (toque del vaso, tara lenta, ruido de peso). **Por defecto 12 s.** Hasta que termina esa ventana (o se confirman las primeras gotas), el stop automático por peso está inhibido. También puede bloquearlo la **ventana de retarificación** si aún está activa. |
| **¿Puedo poner el vaso después de iniciar el shot?** | **Sí**, si **Automatic retare** está activado (por defecto **Sí**). Durante la **ventana de retarificación** (por defecto **4 s** tras el inicio) el firmware detecta una carga estable (vaso ≥ peso mínimo de vaso, p. ej. 10 g) y envía una segunda tara **sin reiniciar** el cronómetro del shot. |
| **¿Por qué el shot no para exactamente en el peso objetivo?** | El firmware usa un **offset de parada aprendido** (por defecto 1,5 g, máx. 5 g) para compensar el goteo post-corte. Además puede parar **por predicción** (regresión sobre muestras recientes) ligeramente antes del umbral directo. En el historial verás `cut_type` y `stop_detail`. |
| **La balanza se desconectó a mitad del shot. ¿Qué pasa?** | En shots automáticos el control por peso se **suspende** y el firmware **sigue intentando reconectar** durante todo el ciclo (no hay lockout permanente a manual tras N segundos). Si recupera tres muestras coherentes, vuelve el stop por peso (incluido Fast extraction guard si aplica) y el **A→M time guard** deja de enforcearse. Mientras la balanza esté caída y el A→M guard esté **ON**, corre un **deadline absoluto desde el inicio del ciclo** (tendencia Auto o límite Manual) y puede **cerrar CN9** antes del límite CN9 — incluso mientras BLE sigue reintentando. En el panel verás `A→M · …s`. Si el guard está **OFF** y la balanza no vuelve, el shot sigue hasta paddle OFF o el wall CN9 / hard 60 s. |
| **Activé “Timer only”. ¿Por qué no para por peso?** | En **modo solo temporizador** se mantiene tara/cronómetro pero se **desactivan** el stop por peso, la retarificación automática, la confirmación de brew y el aprendizaje de offset. El corte depende del paddle, del límite CN9 o de **Stop** remoto. |

## Configuración, red y acceso

| Pregunta | Respuesta |
| --- | --- |
| **¿Cuáles son las credenciales por defecto?** | Red AP: **`MicraShotStopperAP`** / contraseña **`Micra1234`**. Web UI en AP: **`http://192.168.4.1`**. Contraseña de login Web: **`Micra1234`** (misma que el AP). Son **sensibles a mayúsculas**. Tras **factory reset** todo vuelve a estos valores. Detalle: [Factory credentials](../README.md#factory-credentials-first-use). |
| **No encuentro la red del dispositivo tras el primer arranque.** | Sin Wi‑Fi de casa guardado, el firmware levanta el AP **`MicraShotStopperAP`**. Si **nadie inicia sesión**, el AP y el servidor HTTP se apagan a los **3 minutos** (`AP_WINDOW_MS = 180000`) y el Wi‑Fi queda en **OFF** hasta un **reinicio** del ESP32. **Iniciar sesión** cancela ese apagado mientras la sesión siga activa (heartbeat cada ~10 s). Tras **cerrar sesión**, hay otros **3 minutos** de gracia (`UI_GRACE_MS`) antes del apagado. Para guardar Wi‑Fi STA hace falta iniciar sesión (contraseña por defecto **`Micra1234`**) y tener el paddle en OFF sin ciclo activo. Una vez conectado en STA, el AP no se usa y la Web UI permanece disponible sin ese temporizador. El cierre por tiempo solo ocurre con el control en estado seguro (paddle OFF, CN9 abierto). |
| **¿Cómo accedo cuando ya guardé mi Wi‑Fi de casa?** | El dispositivo usa modo **STA**; abre **`http://&lt;ip-del-dispositivo&gt;`**. La IP aparece en los logs serie (**9600** baud) o en la lista DHCP del router. La contraseña Web es la que configuraste (o **`Micra1234`** si no la cambiaste). |
| **¿Puedo cambiar la contraseña?** | Sí, desde la Web UI (**Access point / UI password**) estando autenticado. Cambia la contraseña del AP y del login Web. |
| **¿Puedo controlar el shot desde el móvil?** | La Web UI permite monitorización siempre. **Paddle virtual**, rinse remoto y ciclos nuevos por red requieren compilar con **`SHOT_STOPPER_ENABLE_REMOTE_CN9=1`** (desactivado en builds conservadores). **Stop** remoto abre CN9 en cualquier build autenticado. El **paddle físico siempre tiene prioridad**. |

## Hardware y compatibilidad

| Pregunta | Respuesta |
| --- | --- |
| **¿Qué placas ESP32 puedo usar?** | Probado con **ESP32 Dev Module / DevKit V4** (`esp32:esp32:esp32`) y **ESP32-S3 Dev Module** (`esp32:esp32:esp32s3`). Usa el esquema de particiones **`min_spiffs`** (slot de app ~1,9 MB). Pines por defecto en el [README → Hardware](../README.md#hardware). |
| **¿Qué balanzas son compatibles?** | Vía la librería **AcaiaArduinoBLE**: **Acaia** (Lunar, Pearl S, Pyxis…), **Bookoo** (Themis Mini/Ultra) y **Felicita** (Arc), entre otras. Rendimiento BLE varía por modelo/firmware. Tabla detallada: [Scale Compatibility](../libraries/AcaiaArduinoBLE/README.md#scale-compatibility). |
| **¿Funciona en máquinas distintas a la Micra?** | Este firmware está **diseñado para La Marzocco Linea Micra** (paddle en GPIO, CN9 vía relé aislado). Otras máquinas **no están soportadas oficialmente** aquí; adaptarlas exige **circuito y cableado ad hoc** (relé aislado, lectura del interruptor/paddle, alimentación). La librería original documenta otras máquinas (GS3, Mini, Silvia Pro, etc.) en un [concepto distinto](../libraries/AcaiaArduinoBLE/README.md#espresso-machine-compatibility). |
| **¿Necesito la PCB ShotStopper?** | No es obligatoria: puedes montar ESP32 + relé + LEDs WS2812B siguiendo el README. La PCB simplifica el cableado. |
| **En mi circuito uso GPIO distintos a los del firmware. ¿Cómo los cambio?** | Los pines **no se configuran desde la Web UI** — es una medida de seguridad para evitar asignaciones incorrectas que podrían dejar CN9 cerrado, leer mal el paddle o controlar la máquina de forma peligrosa. Hay que **editar el firmware y recompilar**. Todo vive en **`shotStopper/shotStopper.ino`**, sección *Board hardware* (aprox. líneas 78–194). **Paddle y relé** (`PADDLE_GPIO`, `RELAY_GPIO`): constantes `constexpr` según la placa seleccionada al compilar (`ARDUINO_ESP32_DEV`, `ARDUINO_ESP32S3_DEV`, `ARDUINO_NANO_ESP32`). Si tu placa usa otros pines, modifica el bloque de tu FQBN o añade uno propio. **LEDs WS2812B** (sobreescribibles al compilar con `-D`): `SHOT_STOPPER_SCALE_LED_GPIO`, `SHOT_STOPPER_STOPPER_LED_GPIO`, opcionalmente `SHOT_STOPPER_LED_BRIGHTNESS`. **Seguridad externa K2** (opcional, ambos o ninguno): `SHOT_STOPPER_SAFETY_HEARTBEAT_GPIO`, `SHOT_STOPPER_CN9_FEEDBACK_GPIO`, `SHOT_STOPPER_CN9_FEEDBACK_CLOSED_LEVEL`. **Polaridad** (solo si tu hardware difiere del Micra): `PADDLE_ACTIVE_LEVEL`, `RELAY_CLOSED_LEVEL`, `RELAY_OPEN_LEVEL` — el diseño Micra asume paddle activo **LOW** y relé cerrado **LOW**. El compilador valida con `static_assert` que no haya pines duplicados ni GPIO inválidos. Se asume que quien monta su propia placa conoce su esquema, ajusta el fuente y compila según el [README → Compile](../README.md#compile). Ejemplo solo para LEDs: `compiler.cpp.extra_flags=-DSHOT_STOPPER_SCALE_LED_GPIO=4 -DSHOT_STOPPER_STOPPER_LED_GPIO=5`. |
| **¿Por qué suenan pitidos después de que el shot terminó?** | Con **Scale reminder beep until paddle OFF** (por defecto activo), la balanza repite pitidos mientras el **paddle físico sigue ON** y CN9 ya está abierto — recordatorio de devolver el paddle. Intervalo por defecto **10 s**, límite **15 min**. |

## Seguridad y diagnóstico

| Pregunta | Respuesta |
| --- | --- |
| **CN9 quedó bloqueado / pide “paddle OFF”.** | Tras reset inseguro o fallo, el firmware puede entrar en **REQUIRES_OFF** o **LOCKOUT**. Recuperación: mover el paddle a **ON** y luego mantener **OFF estable ~1 s**. La Web no puede rearmar CN9 sin condiciones seguras. |
| **¿Cómo sé por qué terminó un shot?** | En el **historial de shots**: `cut_type` (`auto`, `manual`, `limit`) y `stop_detail` (`normal_target`, `prediction`, `extended_max_weight`, `auto_to_manual`, etc.). El panel de diagnóstico y `GET /api/v1/log` amplían detalle. |
| **El LED de escala parpadea amarillo pero “está conectada”.** | Amarillo lento = enlace BLE presente pero **worker o stream de peso obsoleto**. No confundir con verde sólido (stream fresco). El comportamiento de CN9 lo gobierna la máquina de estados, no el LED. |
| **¿Es seguro confiar solo en el relé del ESP32?** | **No** como garantía física absoluta. El firmware incluye watchdogs, cierre transaccional y tope de 60 s, pero un relé soldado o un GPIO corto requieren **barrera K2 externa** opcional (heartbeat + feedback aislado). Ver [Watchdog and CN9 safety](../README.md#watchdog-and-cn9-safety). |

## Dónde ajustar cada comportamiento

| Comportamiento | Ajuste en Web UI (Configuration) |
| --- | --- |
| Límite de tiempo del shot (≤ 60 s) | **CN9 limit (s)** |
| Parada temprana/tardía por pérdida de balanza | **A→M time guard** (Enable, Limit mode, Manual limit, Trend, Reset samples) |
| Shot que pasa del objetivo unos gramos | **Fast extraction guard** (Enable, Min brew time, Max recovery) |
| No para al llegar al peso al inicio | **Brew start confirmation (s)** |
| Vaso tardío | **Automatic retare**, **Retare window (s)**, **Minimum cup weight (g)** |
| Sin stop por peso | **Timer only** |
