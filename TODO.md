# TODO — Firmware SiGGECA Etapa II

Contexto: continuación del sistema desplegado en la CAV 2025/2026 (ver
`Informe_CAV_2025_2026_v3.pdf`) para la CAV 2026/2027 (ver
`Propuesta_CAV_2026_2027_limpio.pdf`). Arquitectura: monorepo con un
componente compartido (`components/common`) y dos apps ESP-IDF
independientes (`gateway/`, `remote/`).

## Bloqueante / antes de repartir tareas

- [ ] Verificar compilación real de `gateway` y `remote` con ESP-IDF v6.0.2.
      El scaffolding inicial se armó sin poder compilar (entorno sin
      toolchain); falta correr `idf.py build` en ambas apps y corregir lo
      que aparezca.
- [ ] **Rotar el token de InfluxDB.** El firmware anterior lo tenía
      hardcodeado en texto plano (`metrics_task.h`), expuesto en GitHub. No
      se copió al nuevo repo (`components/common/Kconfig` lo deja vacío por
      default) — hay que generar uno nuevo y cargarlo vía
      `sdkconfig.defaults.local` (gitignored) o NVS en campo.
- [ ] Verificar la polaridad real del relé de antena contra el esquemático
      de HW-ANT-001 — `gateway/main/antenna_ctrl.c` documenta una
      inconsistencia encontrada en el código del año pasado (nivel 1
      documentado como "OFF" en un lado y usado como "ON" en otro).
- [ ] Decidir si el nodo remoto necesita OTA propio (relay de la imagen por
      LoRa a través del gateway) o si se reflashea físicamente en cada
      instalación. Por ahora v1 asume reflash físico (sin OTA en `remote/`).

## Fausto — arquitectura común + FSMs

- [x] Scaffolding de `components/common` (OTA, telemetría InfluxDB,
      logging, watchdog por software, `node_config.h`/Kconfig reemplazando
      los `#ifdef MARAMBIO/ISLA_VEGA` del firmware anterior)
- [x] `gateway_fsm.c` (reemplaza el `while(1)` de `relay_task`)
- [x] `remote_fsm.c` (esqueleto: sleep → wake_acquire → lora_tx → sleep)
- [ ] Bring-up de `gateway_fsm` en devkit desnuda (sin HW nuevo)
- [ ] Bring-up de `remote_fsm` en devkit desnuda (con los stubs de abajo)

## Compañero A — Driver Campbell (PakBus + legacy CR10X)

Archivos: `remote/main/campbell_driver.h` / `.c` (hoy stub con datos
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

Archivos: `gateway/main/lora_rx.h`/`.c` (rol receptor, escucha permanente),
`remote/main/lora_tx.h`/`.c` (rol iniciador).

- [ ] Bring-up del módulo RFM95 en devkit (banda 915 MHz)
- [ ] Definir el formato de paquete real (hoy `remote_fsm.c` envía los
      bytes crudos del struct `campbell_record_t` como placeholder — hace
      falta un formato versionado con id de nodo)
- [ ] Una vez estabilizado, evaluar unificar `lora_rx`/`lora_tx` en un solo
      driver compartido (`components/common/lora_driver.*`) si el código
      queda muy duplicado entre los dos roles

## Compañero C — INA + watchdog HW + control de antena

Archivos: `components/common/{include,src}/wdt_hw.*` (hoy stub no-op),
`gateway/main/antenna_ctrl.*`.

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
