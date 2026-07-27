# Firmware SiGGECA Etapa II

Monorepo de firmware para los dos nodos del sistema (ver
`../Informe_CAV_2025_2026_v3.pdf` y `../Propuesta_CAV_2026_2027_limpio.pdf`
para el contexto del proyecto):

```
components/common/   Componente ESP-IDF compartido: OTA, telemetria InfluxDB,
                      logging por UART, watchdog por software (RTC) y por
                      hardware (stub), y node_config.h (Kconfig: sitio,
                      wifi, timings de antena, claves Victron, InfluxDB, OTA).
gateway/              App ESP-IDF: nodo del refugio (Isla Vega / LAMBI
                      Marambio). Starlink + BLE Victron MPPT + BME280.
remote/               App ESP-IDF: nodo de adquisicion junto al datalogger
                      Campbell (Bahia del Diablo). LoRa, sin Starlink.
```

Ambas apps referencian `components/common` via `EXTRA_COMPONENT_DIRS`
(ver el `CMakeLists.txt` de cada una) -- un cambio ahi se propaga a los dos
nodos sin duplicar codigo.

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

## Estado (ver tambien el plan de arquitectura)

- Portado desde `github.com/lucasliano/esp32-ota-github` (firmware real de
  la CAV 2025/2026): OTA, logging, watchdog por software, BLE Victron,
  BME280, telemetria a InfluxDB, control de rele de antena.
- Nuevo: `gateway_fsm.c`/`remote_fsm.c` (maquinas de estado explicitas,
  reemplazan el `while(1)` ad hoc de `relay_task`), `node_config.h`/Kconfig
  (reemplaza los `#ifdef MARAMBIO/ISLA_VEGA` repetidos en el firmware
  anterior), `telemetry.c` (generaliza los 3 builders duplicados de
  `metrics_task.c` en uno solo).
- Stubs pendientes de completar por sus dueños: `ina`/`wdt_hw`/
  `antenna_ctrl` (INA + watchdog externo + antena), `lora_rx.h`/`lora_tx.h`
  (RFM95), `campbell_driver.h` (PakBus CR1000 + legacy CR10X).

## Pendiente / importante

**Rotar el token de InfluxDB.** El firmware anterior tenia un token de
escritura de InfluxDB hardcodeado en texto plano en `metrics_task.h`,
expuesto en un repo de GitHub. Ese token NO se copio a este repo (ver
`components/common/Kconfig`, que lo deja vacio por default) -- rotarlo en
InfluxDB y proveer el valor real via un `sdkconfig.defaults.local`
(gitignored) o NVS en campo, nunca commiteado.
