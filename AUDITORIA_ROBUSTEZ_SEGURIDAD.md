# Auditoría de robustez y seguridad — Micra Shot Stopper

Fecha: 2026-08-08  
Base revisada: commit e9ed29a y árbol local actual.

## Dictamen

El firmware tiene controles positivos: relé normalmente abierto, arranque con salida abierta, límite de 50 s, watchdog, colas acotadas, validación de configuración y doble slot NVS con CRC. Aun así, no debe considerarse apto para operación desatendida de una máquina peligrosa hasta corregir y validar los hallazgos críticos.

Esta revisión es estática, de pruebas host y compilación. No equivale a una certificación de seguridad ni sustituye protecciones térmicas, de presión y eléctricas independientes.

| Severidad | Hallazgos |
| --- | --- |
| Crítica | F-01, F-02, F-03 |
| Alta | F-04 a F-10 |
| Media | F-11 a F-16 |

## Hallazgos críticos

### F-01 — Características BLE colgantes después de una desconexión

Tipo: defecto confirmado.

Evidencia: AcaiaArduinoBLE asigna características temporales a sus miembros _read y _write. ArduinoBLE 2.1.0 tiene constructor de copia y destructor para BLECharacteristic, pero no asignación de copia. La asignación implícita no retiene la característica. Cuando BLE.poll procesa una desconexión, ArduinoBLE elimina el peer y sus características; el siguiente acceso a los miembros puede usar memoria liberada.

Impacto: crash, corrupción de heap o bloqueo durante pérdida RF, apagado de la báscula o reconexión. El compilador también emite warnings de copia implícita en las cuatro placas.

Corrección mínima: reparar y fijar ArduinoBLE con asignación de copia regla de cinco, o reemplazar el almacenamiento por una forma con propiedad explícita. Centralizar disconnect e invalidación de handles. No silenciar el warning. Probar miles de ciclos conectar/desconectar y cortes RF.

Referencias: [BLECharacteristic](https://github.com/arduino-libraries/ArduinoBLE/blob/2.1.0/src/BLECharacteristic.h), [implementación](https://github.com/arduino-libraries/ArduinoBLE/blob/2.1.0/src/BLECharacteristic.cpp), [eliminación ATT](https://github.com/arduino-libraries/ArduinoBLE/blob/2.1.0/src/utility/ATT.cpp).

### F-02 — Carrera entre armar timers y cerrar CN9

Tipo: defecto confirmado, activable por preempción.

Evidencia: setCn9Closed arma timers antes de publicar cn9Closed, el instante de cierre y el GPIO. Si la tarea se pausa más que el límite entre esas etapas, los callbacks expiran, ven cn9Closed falso y se descartan; al volver la tarea puede cerrar el relé sin timer vigente.

Impacto: se pierde el límite software de cierre CN9.

Corrección mínima: convertir el armado en transacción con estado ARMING, generación y deadline. El callback debe latchear fallo también en ARMING; antes de cerrar, comprobar bajo exclusión que no expiró. Ante cualquier error, escribir salida abierta de forma incondicional. Añadir prueba de inyección de preempción entre cada operación.

### F-03 — El límite de seguridad no es independiente del MCU

Tipo: riesgo arquitectónico confirmado.

Los timers usan ESP_TIMER_TASK y comparten MCU, scheduler, alimentación y software con el control. La documentación de Espressif advierte que el despacho por tarea puede retrasarse por tareas de mayor prioridad u operaciones flash. El GPIO además queda en alta impedancia hasta setup y no existe feedback eléctrico de relé/contacto.

Corrección mínima: añadir monostable, watchdog externo o segundo interlock cableado que abra CN9 sin firmware al exceder el límite. Garantizar pull-down externo y relé desenergizado en reset; añadir feedback de contacto/corriente y fallo latcheado. Un GPTimer ISR/IRAM puede complementar, pero no es independiente.

Referencia: [ESP-IDF esp_timer](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html).

## Hallazgos altos

### F-04 — C3: el loop puede impedir el servicio BLE y red

El loop se eleva a prioridad 3, no cede CPU, escala es prioridad 2 y red prioridad 1. En C3 unicore el core Arduino sólo cede aproximadamente cada dos segundos. ArduinoBLE añade buffers pequeños, envíos con espera infinita y una espera activa HCI.

Corrección: introducir espera acotada o esquema event-driven en loop, revisar prioridades por placa y medir máximo periodo de control, BLE.poll, heap y stacks. No cambiar prioridades sin medir latencia de apertura CN9.

Referencias: [loop Arduino 3.3.3](https://github.com/espressif/arduino-esp32/blob/3.3.3/cores/esp32/main.cpp), [prioridades ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-guides/performance/speed.html), [transporte ArduinoBLE](https://github.com/arduino-libraries/ArduinoBLE/blob/2.1.0/src/utility/HCIVirtualTransport.cpp).

### F-05 — Estado BLE cacheado y teardown incompleto

Varios fallos sólo cambian _connected; no desconectan ni invalidan handles. El timeout de paquetes sólo funciona después del primer paquete, por lo que una desconexión temprana puede quedar marcada como conectada.

Corrección: un único tearDownLink que desconecte, pare scan, limpie handles y timestamps; deadline de adquisición desde conexión y estado real del periférico antes de operar.

### F-06 — Un heartbeat Web puede mantener vivo el ciclo de otro cliente

serviceSessions usa el heartbeat más reciente de cualquier sesión autenticada. Un observador puede evitar el stop por heartbeat de la pestaña que inició el ciclo.

Corrección: lease de control con dueño y generación. Sólo el heartbeat del dueño renueva el ciclo; logout, expiración o reemplazo del dueño debe detenerlo. Probar dos clientes.

### F-07 — El stop depende del flag, no del estado del actuador

setCn9Closed(false) retorna sin escribir GPIO si el flag ya dice abierto. Un flag dañado, GPIO reconfigurado o relé fallado no recibe orden explícita de abrir.

Corrección: escribir siempre el nivel abierto en toda ruta de stop y completar con feedback físico de F-03.

### F-08 — Control Web protegido por contraseña conocida y HTTP

La contraseña de fábrica Micra1234 protege AP y UI; la UI puede cerrar CN9. HTTP en STA expone token y contraseña a una red hostil.

Corrección: contraseña única por dispositivo y cambio obligatorio, habilitación física local para control Web, roles observador/controlador y red aislada o TLS para STA.

### F-09 — Mantenimiento NVS/red puede coincidir con un ciclo

La red comprueba READY mediante snapshot y luego escribe NVS o cambia Wi-Fi. El paddle puede iniciar un ciclo entre ambas fases; flash puede aumentar latencias.

Corrección: lease/ack de mantenimiento con el loop de control: bloquear nuevos ciclos, confirmar seguro, mutar y liberar. No usar snapshot como exclusión mutua.

### F-10 — Configuración aceptada puede no persistirse

El loop aplica runtimeConfig antes de encolar PERSIST_RUNTIME. Si la cola está llena, Web ya aceptó la operación pero un reinicio revierte el valor.

Corrección: revisión dirty coalescida, reintento y confirmación de persistencia; reportar explícitamente si la escritura queda diferida o falla.

## Hallazgos medios

### F-11 — Parser BLE y debug

Se leen como máximo 13 bytes pero, con debug activo, se imprimen longitudes mayores; hay lectura fuera del buffer. El parser acepta paquetes sin validación completa de cabecera, checksum, unidad, dígitos o rango físico.

Corrección: imprimir bytes realmente leídos y validar protocolo, peso finito, rango y tasa de cambio antes de alimentar el predictor.

### F-12 — Capacidades de protocolo incompletas

tareStartTimer transmite comando Bookoo sin comprobar tipo. beep clásico no es seguro para todas las básculas; beepWithoutStateChange es la ruta segura actual.

Corrección: modelar capacidades por tipo y negar comando combinado fuera de Bookoo; conservar exclusivamente el beep no-tare para avisos.

### F-13 — Colas y workers sin supervisión completa

Eventos críticos usan espera infinita y STOP remoto puede perderse si la cola está llena; CN9 abre pero el timer remoto puede quedar activo. Sólo loop tiene watchdog.

Corrección: reservar/coalescer STOP, usar espera finita, medir backlog y supervisar progreso y stack de cada worker.

### F-14 — Persistencia binaria frágil ante layout y wear

El CRC cubre struct en memoria, incluyendo layout/padding, y no hay lectura de verificación tras escribir. El offset aprendido puede escribir NVS con frecuencia.

Corrección: serialización canónica de campos de ancho fijo, bytes reservados inicializados, verificación post-escritura y límite por epsilon/tiempo para offset.

### F-15 — Recuperación de inicialización incompleta

Fallo inicial de AP/HTTP puede dejar startup completo sin reintento. Se ignoran resultados de EEPROM.begin; ArduinoBLE ESP32 puede informar éxito pese a fallos internos.

Corrección: estado observable y reintento backoff seguro para red; modo manual explícito ante fallo BLE/persistencia.

### F-16 — Pruebas reales y build reproducible

Las pruebas host sustituyen RTOS, BLE y HTTP; no cubren carrera BLE, planificación C3 ni sesiones reales. La dependencia ArduinoBLE no queda fijada por library.properties y el comando README necesita --library . en checkout limpio.

Corrección: CI con core 3.3.3 y ArduinoBLE 2.1.0 fijados, compilación de todas las FQBN y warnings del proyecto como errores. Añadir HIL de reconexión BLE, RF degradada, dos clientes Web, power-cut NVS, brownout y soak 72 h.

## Validación realizada

- 78 pruebas host de control, repetidas con ASan/UBSan: correctas.
- 11 pruebas de persistencia, repetidas con ASan/UBSan: correctas.
- Sintaxis del WebUI embebido: correcta.
- Compilación ESP32 core 3.3.3 y ArduinoBLE 2.1.0: ESP32 92 % flash, ESP32-C3 96 %, ESP32-S3 87 %.

Los stubs host no pueden refutar F-01 a F-04.

## Orden recomendado

1. Corregir F-01 y F-02 y probar desconexión/preempción.
2. Implantar F-03 antes de uso sin supervisión.
3. Resolver F-04 y F-05 para estabilidad de días, sobre todo C3.
4. Resolver F-06 a F-10 antes de habilitar control Web en producción.
5. Cerrar F-11 a F-16 con HIL y soak.

## Cambios no recomendados sin evidencia

- No aumentar el límite duro de 50 s.
- No reescribir la máquina de estados ni sustituir todas las colas: aportan aislamiento útil.
- No convertir buffers fijos válidos a String como supuesto arreglo de robustez.
- No actualizar dependencias a ciegas: primero fijar y probar la corrección de propiedad BLE.

