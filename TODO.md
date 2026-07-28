# TODO — Firmware SiGGECA Etapa II

Contexto: continuación del sistema desplegado en la CAV 2025/2026 para la
CAV 2026/2027 (Informe y Propuesta compartidos con GIAR/IAA/DNA).
Arquitectura: monorepo con un componente compartido (`components/common`,
`inc/`+`src/`) y dos apps ESP-IDF independientes (`gateway/`, `remote/`,
cada una con su propio `inc/`+`src/`).

## Bloqueante / antes de repartir tareas

- [ ] Verificar compilación real de `gateway` y `remote` con ESP-IDF v6.0.2.
      El scaffolding se armó sin poder compilar en esta sesión (entorno sin
      toolchain); falta correr `idf.py build` en ambas apps y corregir lo
      que aparezca.
- [ ] **Rotar el token de InfluxDB.** El firmware anterior lo tenía
      hardcodeado en texto plano (`metrics_task.h`), expuesto en GitHub. No
      se copió al nuevo repo (`components/common/Kconfig` lo deja vacío por
      default) — hay que generar uno nuevo y cargarlo vía
      `sdkconfig.defaults.local` (gitignored) o NVS en campo.
- [ ] Verificar la polaridad real del relé de antena contra el esquemático
      de HW-ANT-001 — `gateway/main/src/antenna_ctrl.c` documenta una
      inconsistencia encontrada en el código del año pasado (nivel 1
      documentado como "OFF" en un lado y usado como "ON" en otro).
- [ ] Decidir si el nodo remoto necesita OTA propio (relay de la imagen por
      LoRa a través del gateway) o si se reflashea físicamente en cada
      instalación. Por ahora v1 asume reflash físico (sin OTA en `remote/`).

## Fausto — arquitectura común + FSMs

- [x] Scaffolding de `components/common` (OTA, telemetría InfluxDB, BME280,
      logging, watchdog por software, `node_config.h`/Kconfig reemplazando
      los `#ifdef MARAMBIO/ISLA_VEGA` del firmware anterior)
- [x] `gateway_fsm.c` (reemplaza el `while(1)` de `relay_task`)
- [x] `remote_fsm.c` (esqueleto: sleep → wake_acquire → lora_tx → sleep)
- [x] BME280 compartido entre gateway y remoto (`bme280_task()` para el
      gateway, que empuja a InfluxDB por wifi; `bme280_read()` puntual para
      el remoto, que no tiene salida a internet y empaqueta el dato junto
      con Campbell en el paquete LoRa — ver `remote_payload_t` en
      `remote_fsm.c`)
- [x] Tag `role=` (`SIGGECA_NODE_ROLE`) en la telemetría para distinguir
      gateway vs remoto aunque compartan sitio
- [ ] Bring-up de `gateway_fsm` en devkit desnuda (sin HW nuevo)
- [ ] Bring-up de `remote_fsm` en devkit desnuda (con los stubs de abajo)

## Compañero A — Driver Campbell (PakBus + legacy CR10X)

Archivos: `remote/main/inc|src/campbell_driver.*` (hoy stub con datos
simulados).

- [ ] Backend PakBus contra el CR1000 prestado por el IAA (referencia:
      `github.com/LionelDarras/PyCampbellCR1000`, protocolo que pasó Arturo
      Huber)
- [ ] Definir el layout real de `campbell_record_t` (los campos actuales —
      temperatura, humedad, viento — son placeholder, hay que confirmar
      contra la tabla real de la estación de Bahía del Diablo)
- [ ] Backend legacy para el CR10X real (solo validable en campo — mayor
      riesgo técnico según la propuesta CAV 2026/2027)

## Compañero B — Driver LoRa (RFM95)

Archivos: `gateway/main/inc|src/lora_rx.*` (rol receptor, escucha
permanente), `remote/main/inc|src/lora_tx.*` (rol iniciador).

- [ ] Bring-up del módulo RFM95 en devkit (banda 915 MHz)
- [ ] Definir el formato de paquete real (hoy `remote_fsm.c` envía los
      bytes crudos de `remote_payload_t` — campbell + bme280 — como
      placeholder; hace falta un formato versionado con id de nodo)
- [ ] Una vez estabilizado, evaluar unificar `lora_rx`/`lora_tx` en un solo
      driver compartido (`components/common/lora_driver.*`) si el código
      queda muy duplicado entre los dos roles

## Compañero C — INA + watchdog HW + control de antena

Archivos: `components/common/inc|src/wdt_hw.*` (hoy stub no-op),
`gateway/main/inc|src/antenna_ctrl.*`.

- [ ] Mapeo de pines definitivo contra el esquemático final de HW-ANT-001
- [ ] Implementar `wdt_hw.c` (kick real del watchdog externo)
- [ ] Agregar lectura de corriente INA (nuevo módulo, o extensión de
      `antenna_ctrl`)
- [ ] Verificar la polaridad del relé (ver bloqueante arriba)

## Integración final

- [ ] Reemplazar los stubs en ambas FSM por las implementaciones reales a
      medida que estén listas
- [ ] Repetir el bring-up manual en devkit con cada driver real antes de
      esperar al PCB final
