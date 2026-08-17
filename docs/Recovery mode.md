 # Recuperación local mediante paddle

## Resumen

Implementar dos gestos locales durante una ventana de recuperación de 60 segundos:

- Tres ciclos `OFF→ON`: reset de Wi‑Fi, AP y contraseña.
- Cinco ciclos `OFF→ON`: factory reset completo.

No se implementará recuperación mediante cortes de energía.

## Entrada y seguridad

- Entrar únicamente tras un encendido real con el paddle ON estable durante el debounce de 30 ms.
- No activarlo después de reinicios por software, watchdog o panic.
- Emitir un beep local continuo de 1,5 s al entrar.
- Mantener el modo durante un máximo total de 60 s.
- Durante recuperación:
  - CN9 permanecerá siempre abierto.
  - No se iniciarán Wi‑Fi, BLE ni Web UI.
  - Se atenderán el watchdog, supervisor del relé, paddle y buzzer.
  - Ningún movimiento podrá iniciar un brew o rinse.
- Al expirar los 60 s sin una confirmación válida, emitir otro beep de 1,5 s y continuar el arranque normal. Si el paddle sigue ON, permanecer en `REQUIRES_OFF`.

## Reconocimiento de gestos

- Contar ciclos completos `OFF→ON`, partiendo desde el paddle ON.
- Todos los ciclos deben ocurrir dentro de 5 s desde el primer movimiento a OFF.
- Después del último movimiento, exigir 3 s continuos sin cambios:
  - Exactamente tres ciclos: confirmar gesto corto.
  - Exactamente cinco ciclos: confirmar gesto largo.
- El temporizador de confirmación se reinicia con cada transición:
  - Después del tercer ciclo, el usuario puede continuar hasta cinco.
  - Al completar cinco ciclos dentro de la ventana original de 5 s, el candidato cambia a factory reset y comienza una nueva confirmación de 3 s.
- Uno, dos, cuatro o más de cinco ciclos constituyen un intento inválido.
- Un movimiento que haga expirar la ventana de 5 s invalida el intento.
- Tras un intento inválido, reiniciar el contador y permitir otro mientras queden segundos dentro de los 60 s.
- El gesto y su confirmación deben completarse antes del límite total de 60 s.

## Acciones confirmadas

### Gesto corto

- Restablecer exclusivamente el acceso de red:
  - Borrar SSID y contraseña STA.
  - Restaurar DHCP y borrar IP estática.
  - Borrar la configuración last-known-good.
  - Restaurar la contraseña del AP/Web UI a `Micra1234`.
- Preservar configuración de máquina, presets, calibración, escalas e historial.
- Verificar los datos persistidos.
- Emitir tres pulsos de 50 ms sonido / 50 ms pausa para confirmar éxito.
- Reiniciar el ESP32.

### Gesto largo

- Restaurar configuración, presets, calibración, red, AP/Web UI, escalas y BLE Companion a valores de fábrica.
- Borrar historial y último shot.
- Verificar todos los almacenes persistentes.
- Emitir cinco pulsos de 50 ms sonido / 50 ms pausa para confirmar éxito.
- Reiniciar el ESP32.

En builds sin buzzer, los procedimientos funcionarán silenciosamente. Las alertas de recuperación ignorarán la configuración normal de sonidos y no usarán la balanza.

## Resiliencia

- Registrar una intención persistente y verificada antes de modificar datos.
- Hacer cada operación idempotente para reanudarla después de un corte de energía.
- Completar cualquier reset pendiente en el siguiente arranque antes de iniciar red o BLE.
- Eliminar la intención solamente después de verificar todos los cambios.
- Ante un fallo NVS, no confirmar éxito ni reiniciar; mantener CN9 abierto, conservar la operación pendiente y emitir una señal de error diferenciada.
- Reutilizar los helpers existentes de persistencia y factory reset.
- No cambiar las interfaces REST o BLE públicas.

## Documentación de emergencia

- Crear [docs/EMERGENCY_RECOVERY.md](/Users/felipe/Repos/AcaiaArduinoBLE/docs/EMERGENCY_RECOVERY.md) como manual específico y autocontenido.
- Incluir:
  - Cuándo utilizar la recuperación física.
  - Advertencia de que CN9 permanece abierto y no se puede preparar café durante el procedimiento.
  - Diferencias entre reset de acceso y factory reset.
  - Datos preservados y eliminados por cada alternativa.
  - Requisitos de posición inicial y significado de un ciclo `OFF→ON`.
  - Instrucciones numeradas para encender con paddle ON.
  - Explicación del beep de entrada, ventana de 60 s, límite de 5 s y confirmación inmóvil de 3 s.
  - Diagramas textuales exactos:
    - Corto: `OFF→ON ×3`, esperar 3 s sin mover.
    - Largo: `OFF→ON ×5`, esperar 3 s sin mover.
  - Ejemplos cronológicos con segundos aproximados para ambos procedimientos.
  - Cantidad de pulsos que confirma cada resultado.
  - Credenciales posteriores: `MicraShotStopperAP`, `Micra1234` y `http://192.168.4.1`.
  - Comportamiento de builds sin buzzer.
  - Errores habituales: movimientos lentos, cuatro ciclos, mover durante la confirmación, agotar los 60 s o arrancar con paddle OFF.
  - Cómo cancelar de forma segura: dejar expirar la ventana, llevar el paddle a OFF y esperar el arranque normal.
  - Qué hacer si no reinicia o no aparece el AP.
- Añadir en el README una sección breve “Emergency recovery” con:
  - Resumen de ambos gestos.
  - Advertencia sobre pérdida de datos del factory reset.
  - Enlace visible al manual detallado.
- Añadir enlaces al mismo manual desde FAQ y desde las instrucciones existentes de recuperación por USB, aclarando que el paddle es la alternativa cuando no hay acceso por red, BLE o serie.

## Pruebas

- Entrada con paddle ON/OFF y exclusión de reinicios que no sean `POWERON`.
- Gestos de tres y cinco ciclos, incluyendo la conversión del candidato corto en largo.
- Secuencias de cuatro, seis, incompletas, lentas, con rebotes o movimientos durante la confirmación.
- Límites exactos de 5 s, 3 s y 60 s, incluyendo wraparound de `millis()`.
- Verificar que CN9 nunca cierre durante espera, confirmación, persistencia, fallo o reinicio.
- Confirmar que el gesto corto preserva todos los datos ajenos a red/acceso.
- Confirmar que el gesto largo restaura todos los almacenes.
- Cortar energía entre etapas y comprobar la reanudación idempotente.
- Probar buzzer pasivo, activo y deshabilitado.
- Ejecutar la suite host completa y compilar para todas las placas soportadas.
- Añadir casos al plan manual que sigan literalmente `EMERGENCY_RECOVERY.md` y comprueben que README/FAQ enlazan correctamente al documento.
