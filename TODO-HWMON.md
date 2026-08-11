### Requisito: Monitoreo de hardware y estado del controlador

El sistema deberá disponer de una sección denominada **“Monitoreo de Hardware”** dentro de la interfaz web del controlador, cuyo objetivo será permitir la supervisión del estado operativo, térmico, de memoria y de conectividad del ESP32, facilitando la detección de problemas de estabilidad, temperatura, consumo de memoria y comunicaciones.

La información deberá obtenerse directamente desde el ESP32 y actualizarse periódicamente mientras la sección se encuentre visible, sin requerir recargar manualmente la página. La obtención y visualización de estas métricas no deberá interferir con las funciones críticas del controlador, particularmente la lectura del paddle, el control del relé, la ejecución de shots y la comunicación BLE con la balanza.

#### 1. Estado general

La sección deberá mostrar:

- **Uptime:** tiempo transcurrido desde el último arranque o reinicio del controlador, expresado en días, horas, minutos y segundos.
- **Motivo del último reset:** causa reportada por el ESP32 para el último arranque, identificando al menos cuando sea posible:
  - Power-on.
  - Reset por software.
  - Watchdog.
  - Brownout.
  - Reset externo.
  - Otras causas proporcionadas por el ESP32.

#### 2. Monitoreo térmico

El sistema deberá mostrar:

- **Temperatura actual del ESP32**, expresada en °C.
- **Temperatura máxima registrada desde el último arranque**, expresada en °C.

La temperatura máxima deberá mantenerse en memoria durante toda la ejecución y solo reiniciarse cuando el ESP32 sea reiniciado.

La interfaz deberá indicar que esta temperatura corresponde al sensor interno del ESP32 y, por lo tanto, representa una estimación de la temperatura interna del chip y no una medición precisa de la temperatura ambiente.

#### 3. Memoria RAM interna

El sistema deberá mostrar como mínimo:

- **RAM/Heap total disponible.**
- **RAM/Heap libre actual.**
- **RAM/Heap libre mínima registrada desde el arranque.**
- **Mayor bloque contiguo de memoria actualmente disponible.**

Los valores deberán presentarse utilizando KB o MB según corresponda.

La métrica de RAM libre mínima deberá representar el menor valor registrado desde el último arranque y permitirá detectar picos de utilización de memoria aunque estos ya no estén presentes al momento de consultar la interfaz.

El mayor bloque libre deberá mostrarse independientemente de la RAM libre total, permitiendo identificar posibles situaciones de fragmentación de memoria.

#### 4. PSRAM

Cuando la placa utilizada disponga de PSRAM, el sistema deberá mostrar:

- **PSRAM total.**
- **PSRAM libre actual.**
- **PSRAM libre mínima registrada desde el arranque**, cuando sea técnicamente posible obtenerla o mantenerla mediante software.

Si la placa no dispone de PSRAM o esta no se encuentra disponible, la interfaz deberá indicarlo explícitamente en lugar de mostrar valores inválidos.

#### 5. Conectividad Wi-Fi

El sistema deberá mostrar:

- **Estado de conexión Wi-Fi:** conectado/desconectado.
- **RSSI actual**, expresado en dBm.
- **SSID de la red actualmente utilizada.**
- **Dirección IP asignada al controlador.**
- **Cantidad de desconexiones/reconexiones Wi-Fi registradas desde el último arranque.**

El contador deberá permitir identificar problemas de estabilidad de la conexión incluso cuando Wi-Fi se encuentre conectado al momento de consultar la interfaz.

#### 6. Conectividad BLE de la balanza

El sistema deberá mostrar:

- **Estado de la conexión BLE con la balanza:** conectada, desconectada, conectando o buscando.
- **Nombre o identificador de la balanza conectada**, cuando esté disponible.
- **RSSI BLE de la balanza**, cuando la API utilizada permita obtenerlo de manera confiable.
- **Cantidad de desconexiones/reconexiones BLE desde el último arranque.**

Los contadores deberán mantenerse durante toda la ejecución y reiniciarse únicamente con el reinicio del controlador.

#### 7. Presentación de la información

La interfaz deberá organizar las métricas en al menos tres grupos visuales:

**Sistema**
- Uptime.
- Temperatura actual.
- Temperatura máxima.
- Motivo del último reset.

**Memoria**
- RAM total.
- RAM libre.
- RAM libre mínima.
- Mayor bloque libre.
- PSRAM total.
- PSRAM libre.
- PSRAM libre mínima, cuando corresponda.

**Conectividad**
- Estado Wi-Fi.
- SSID.
- Dirección IP.
- RSSI Wi-Fi.
- Reconexiones Wi-Fi.
- Estado BLE de la balanza.
- Identificación de la balanza.
- RSSI BLE, cuando esté disponible.
- Reconexiones BLE.

La información deberá presentarse de manera compacta y legible, diferenciando claramente los valores actuales de los valores históricos registrados desde el arranque.

#### 8. Actualización de métricas

Las métricas deberán actualizarse automáticamente mientras la página de monitoreo se encuentre abierta.

La frecuencia de actualización deberá ser suficiente para proporcionar información útil de diagnóstico sin generar una carga significativa sobre el ESP32. Como referencia, se recomienda una actualización de aproximadamente **cada 2 a 5 segundos**.

No será necesario mantener un historial temporal permanente de las métricas. Los valores históricos requeridos serán únicamente máximos, mínimos y contadores acumulados desde el último arranque.

#### 9. Persistencia

Las métricas históricas de esta sección serán consideradas información de diagnóstico de la ejecución actual y, por defecto, **no deberán persistirse en memoria Flash/NVS**.

Por lo tanto, al reiniciar el ESP32 deberán reiniciarse:

- Temperatura máxima.
- RAM libre mínima mantenida por la aplicación, cuando corresponda.
- PSRAM libre mínima.
- Contador de reconexiones Wi-Fi.
- Contador de reconexiones BLE.

Esto evitará escrituras innecesarias en la memoria Flash.

#### 10. Impacto sobre las funciones críticas

El monitoreo deberá implementarse como una función secundaria y de baja prioridad.

La recopilación, procesamiento o transmisión de las métricas no deberá bloquear ni retrasar las funciones críticas del controlador, incluyendo:

- Lectura del estado del paddle.
- Activación o desactivación del relé.
- Inicio y detención de shots.
- Brew by Weight/Brew by Time.
- Recepción de peso mediante BLE.
- Funciones de seguridad asociadas al control de la máquina.

La indisponibilidad de alguna métrica de diagnóstico no deberá impedir el funcionamiento normal del controlador.