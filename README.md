# Firmware SiGGECA Etapa II

Monorepo de firmware para los dos nodos del sistema (contexto completo en
el Informe CAV 2025/2026 y la Propuesta CAV 2026/2027 compartidos con
GIAR/IAA/DNA -- no forman parte de este repo):

```
components/common/   Componente ESP-IDF compartido: OTA, telemetria InfluxDB,
                      BME280, logging por UART, watchdog por software (RTC) y
                      por hardware (stub), y node_config.h (Kconfig: sitio,
                      rol de nodo, wifi, timings de antena, claves Victron,
                      InfluxDB, OTA).
gateway/              App ESP-IDF: nodo del refugio (Isla Vega / LAMBI
                      Marambio). Starlink + BLE Victron MPPT + BME280.
remote/               App ESP-IDF: nodo de adquisicion junto al datalogger
                      Campbell (Bahia del Diablo). LoRa + BME280, sin Starlink.
```

Cada app (`gateway/`, `remote/`) y el componente `components/common` tienen
la misma convencion interna: `inc/` para headers, `src/` para fuentes
(`src/ble/` dentro de `gateway/main` agrupa lo especifico de BLE Victron,
igual que en el firmware anterior).

Ambas apps referencian `components/common` via `EXTRA_COMPONENT_DIRS`
(ver el `CMakeLists.txt` de cada una) -- un cambio ahi se propaga a los dos
nodos sin duplicar codigo. Cada app trae su propio `sdkconfig.defaults` que
fija su rol (`SIGGECA_NODE_ROLE_GATEWAY`/`_REMOTE`) para que la telemetria
en InfluxDB los distinga aunque compartan el mismo `SIGGECA_SITE` (loc=).

## Build

Requiere ESP-IDF instalado y exportado (`. $IDF_PATH/export.sh` o
`export.ps1`). No se pudo verificar la compilacion en esta sesion (sin
toolchain disponible) -- revisar con `idf.py build` antes de dar esto por
andando.

```
cd gateway && idf.py set-target esp32 && idf.py menuconfig   # elegir sitio, wifi, etc.
idf.py build

cd ../remote && idf.py set-target esp32
idf.py build
```

## Estado

- Portado desde `github.com/lucasliano/esp32-ota-github` (firmware real de
  la CAV 2025/2026): OTA, logging, watchdog por software, BLE Victron,
  BME280, telemetria a InfluxDB, control de rele de antena.
- Nuevo: `gateway_fsm.c`/`remote_fsm.c` (maquinas de estado explicitas,
  reemplazan el `while(1)` ad hoc de `relay_task`), `node_config.h`/Kconfig
  (reemplaza los `#ifdef MARAMBIO/ISLA_VEGA` repetidos en el firmware
  anterior), `telemetry.c` (generaliza los 3 builders duplicados de
  `metrics_task.c` en uno solo).
- BME280 es compartido: `bme280_task()` (periodico, push directo a InfluxDB)
  es solo para el gateway, que tiene salida a internet. El nodo remoto usa
  `bme280_read()` (lectura puntual, sin red) desde `remote_fsm.c` y empaqueta
  el resultado junto con el dato de Campbell en el paquete LoRa.
- Enlace LoRa (RFM95/SX1276) entre remoto y gateway con OTA del remoto por
  LoRa, a partir del trabajo de Aylen (`aylen27s/ota-by-lora`). El driver
  y la trama viven en `components/common`. Ver `docs/LORA.md`.
- Stubs pendientes de completar por sus dueños: `ina`/`wdt_hw`/
  `antenna_ctrl` (INA + watchdog externo + antena), `campbell_driver.h`
  (PakBus CR1000 + legacy CR10X).

Ver `TODO.md` para el detalle de tareas por persona.

## Pendiente / importante

**Rotar el token de InfluxDB.** El firmware anterior lo tenia hardcodeado
en texto plano en `metrics_task.h`, expuesto en un repo de GitHub. Ese
token NO se copio a este repo (`components/common/Kconfig` lo deja vacio
por default) -- rotarlo en InfluxDB y proveer el valor real via un
`sdkconfig.defaults.local` (gitignored) o NVS en campo, nunca commiteado.
