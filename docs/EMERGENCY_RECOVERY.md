# Recuperación de emergencia con el paddle

Este procedimiento permite recuperar el Micra Shot Stopper cuando no hay
acceso por Web UI, Wi-Fi, BLE ni USB/serie. No requiere que la balanza esté
encendida o conectada.

> **Seguridad:** durante la recuperación, el firmware mantiene CN9 abierto y
> no permite iniciar una extracción ni un rinse. No intentes preparar café
> hasta que el procedimiento termine y el controlador vuelva a arrancar.

## Elegir el procedimiento

| Procedimiento | Gesto | Borra | Conserva |
| --- | --- | --- | --- |
| Recuperar acceso | `OFF→ON ×3` | Wi-Fi STA, IP estática, red last-known-good y contraseña del AP/Web UI | Configuración de máquina, presets, calibración, escalas e historial |
| Factory reset | `OFF→ON ×5` | Toda la configuración, red, calibración, escalas, BLE Companion, historial y último shot | Solo el firmware instalado |

Después de cualquiera de los dos procedimientos, el acceso local vuelve a:

- Red: **`MicraShotStopperAP`**
- Contraseña del AP y Web UI: **`Micra1234`**
- Dirección: **`http://192.168.4.1`**

Las contraseñas distinguen mayúsculas de minúsculas.

## Antes de comenzar

Un ciclo significa mover el paddle completamente de **OFF a ON**. El
controlador debe encender inicialmente con el paddle en **ON**; esa posición
inicial no cuenta como un ciclo.

- Haz todos los movimientos del gesto en menos de 5 segundos.
- Después del último ON, no muevas el paddle durante 3 segundos.
- El modo de recuperación dura 60 segundos en total.
- Si el firmware se compiló sin buzzer, los mismos pasos funcionan sin sonido.

## Recuperar Wi-Fi, AP y contraseña

Este procedimiento no borra recetas, ajustes de la máquina ni historial.

1. Desenergiza el Shot Stopper.
2. Lleva el paddle a **ON**.
3. Energiza el Shot Stopper manteniendo el paddle en ON.
4. Espera el beep continuo de 1,5 segundos que anuncia el modo de recuperación.
5. Dentro de los 60 segundos, realiza tres ciclos completos en menos de 5
   segundos:

   ```text
   Posición inicial: ON
   OFF → ON → OFF → ON → OFF → ON
          ciclo 1    ciclo 2    ciclo 3
   ```

6. Deja el paddle inmóvil en ON durante 3 segundos.
7. Tres pulsos cortos confirman que los datos de acceso fueron restaurados.
8. Espera el reinicio y conecta a `MicraShotStopperAP` con `Micra1234`.

Ejemplo de tiempos válidos:

```text
0,0 s  primer OFF
0,5 s  primer ON
1,0 s  segundo OFF
1,5 s  segundo ON
2,0 s  tercer OFF
2,5 s  tercer ON
5,5 s  termina la confirmación inmóvil; reset de acceso
```

## Ejecutar un factory reset

> **Advertencia:** este procedimiento elimina presets, configuración,
> calibración aprendida, redes, escalas e historial. No se puede deshacer.

1. Desenergiza el Shot Stopper.
2. Lleva el paddle a **ON**.
3. Energiza el Shot Stopper manteniendo el paddle en ON.
4. Espera el beep continuo de 1,5 segundos.
5. Dentro de los 60 segundos, realiza cinco ciclos completos en menos de 5
   segundos:

   ```text
   Posición inicial: ON
   OFF → ON → OFF → ON → OFF → ON → OFF → ON → OFF → ON
          ciclo 1    ciclo 2    ciclo 3    ciclo 4    ciclo 5
   ```

6. Deja el paddle inmóvil en ON durante 3 segundos.
7. Cinco pulsos cortos confirman el factory reset.
8. Espera el reinicio y realiza la puesta en marcha desde
   `http://192.168.4.1`.

Ejemplo de tiempos válidos:

```text
0,0 s  primer OFF
0,4 s  primer ON
0,8 s  segundo OFF
1,2 s  segundo ON
1,6 s  tercer OFF
2,0 s  tercer ON
2,4 s  cuarto OFF
2,8 s  cuarto ON
3,2 s  quinto OFF
3,6 s  quinto ON
6,6 s  termina la confirmación inmóvil; factory reset
```

Los primeros tres ciclos del gesto largo se parecen al gesto corto. No hay
riesgo de que se aplique prematuramente: cualquier movimiento reinicia la
espera de confirmación, y el firmware decide solo después de 3 segundos sin
movimientos.

## Cancelar sin borrar datos

Deja de mover el paddle y permite que expire la ventana total de 60 segundos.
Un beep de 1,5 segundos anuncia la salida. Lleva después el paddle a OFF; el
firmware continuará su arranque normal y CN9 seguirá abierto hasta detectar
ese OFF estable.

También puedes cortar la energía antes de que finalicen los 3 segundos de
confirmación. Si el borrado ya había comenzado, una intención persistente hará
que el siguiente arranque complete la operación de forma segura.

## Errores habituales

- **Arrancar con paddle OFF:** inicia normalmente; no entra en recuperación.
- **Moverlo demasiado lento:** si los ciclos toman más de 5 segundos, el
  intento se invalida. Puedes volver a intentarlo dentro de los 60 segundos.
- **Hacer cuatro ciclos:** no corresponde a ningún comando y no borra datos.
- **Mover durante los 3 segundos:** reinicia la confirmación o convierte el
  gesto corto en el largo si se completan cinco ciclos a tiempo.
- **Agotar los 60 segundos:** el modo termina sin ejecutar un reset.
- **No escuchar beeps:** el build puede no incluir buzzer. Cuenta los
  movimientos y tiempos igualmente.

## Si no reinicia o no aparece el AP

1. Espera al menos 20 segundos después de la confirmación.
2. Comprueba que el paddle esté en OFF y vuelve a energizar el controlador.
3. Busca `MicraShotStopperAP`; el intento STA inicial puede demorar su
   aparición aproximadamente 15 segundos.
4. Si se oye un patrón largo-corto-largo y no continúa el arranque, hubo un
   fallo de almacenamiento. CN9 permanece abierto. Corta y restablece energía
   para reintentar automáticamente la operación pendiente.
5. Si el problema persiste, usa el [CLI USB](SERIAL_CLI.md) o reflashea el
   firmware antes de conectar CN9 nuevamente.

