# TODO — Firmware SiGGECA Etapa II

Contexto: continuación del sistema desplegado en la CAV 2025/2026 para la
CAV 2026/2027 (Informe y Propuesta compartidos con GIAR/IAA/DNA).
Arquitectura: monorepo con un componente compartido (`components/common`,
`inc/`+`src/`) y dos apps ESP-IDF independientes (`gateway/`, `remote/`,
cada una con su propio `inc/`+`src/`).

## Bloqueante / antes de repartir tareas

- [x] Verificar compilación real de `gateway` y `remote` con ESP-IDF v6.0.2.
      Ambas compilan limpio. Apareció y se corrigió una serie de problemas
      reales por el salto de versión (esta instalación trae ESP-IDF 6.0.2 /
      mbedtls 4.1.0, más nueva que con la que se escribió el firmware
      original):
      - `uart_console.h`/`bme280.h` incluían `driver/gpio.h` de más (solo
        usaban constantes `GPIO_NUM_x`, que ahora vienen de
        `hal/gpio_types.h`) — el componente `common` no declaraba
        `esp_driver_gpio` como dependencia.
      - `ota.c`: el switch de eventos HTTP no cubría dos valores nuevos del
        enum (`-Werror=switch`) — se agregó un `default`.
      - **mbedtls 4 eliminó `mbedtls/aes.h`** (API clásica) — todo pasa por
        PSA Crypto ahora. Se reescribió `victron_crypto.c` con
        `psa_cipher_decrypt_setup`/`psa_cipher_set_iv`/`psa_cipher_update`/
        `psa_cipher_finish` en vez de `mbedtls_aes_crypt_ctr`. **Revisar con
        cuidado** — es el descifrado real del MPPT Victron, y hay que
        confirmar en banco que el payload decodificado sigue siendo
        correcto contra un paquete real.
      - Faltaba habilitar NimBLE (`CONFIG_BT_ENABLED`/`CONFIG_BT_NIMBLE_ENABLED`,
        ahora en `gateway/sdkconfig.defaults`) para que `nimble/*.h` exista.
      - El partition table por defecto (~1MB) no entra el binario del
        gateway (BLE+wifi+mbedtls+https_ota ≈ 1.26MB) — se agregó
        `gateway/partitions.csv` (dos slots OTA de 1.5MB) asumiendo **flash
        de 4MB**; confirmar contra el chip real cuando llegue el HW
        (`esptool.py flash_id`) y ajustar si no coincide.
- [ ] **Rotar el token de InfluxDB.** El firmware anterior lo tenía
      hardcodeado en texto plano (`metrics_task.h`), expuesto en GitHub. No
      se copió al nuevo repo (`components/common/Kconfig` lo deja vacío por
      default) — hay que generar uno nuevo y cargarlo vía
      `sdkconfig.defaults.local` (gitignored) o NVS en campo.
- [ ] Verificar la polaridad real del relé de antena contra el esquemático
      de HW-ANT-001 — `gateway/main/src/antenna_ctrl.c` documenta una
      inconsistencia encontrada en el código del año pasado (nivel 1
      documentado como "OFF" en un lado y usado como "ON" en otro).
- [x] Decidir si el nodo remoto necesita OTA propio: **sí, relay por LoRa a
      través del gateway**, disparado por el remoto en el ACK de su
      telemetría (no rompe el esquema "el remoto siempre inicia"). Ver
      `docs/LORA.md` (protocolo, puntos de falla y mitigaciones).

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
      Huber). El CR1000 es autodescriptivo: PakBus permite pedirle al logger
      la definición de sus tablas/campos en runtime, no hace falta
      hardcodear el layout.
- [ ] **Bloqueante para el backend legacy**: pedirle a Arturo Huber / IAA
      Glaciología el programa Edlog actual del CR10X de Bahía del Diablo (o
      al menos la documentación de sus "Output Array" — qué Array ID
      corresponde a qué variable y en qué orden). A diferencia del CR1000,
      el CR10X **no transmite esa información por protocolo** — el Final
      Storage es un área plana identificada solo por Array ID, y sin el
      programa Edlog no hay forma de saber qué campo es cada cosa dentro de
      un array. Sin este dato el backend legacy queda bloqueado
      independientemente de qué tan bien esté hecho el resto.
- [ ] Definir el layout real de `campbell_record_t` (los campos actuales —
      temperatura, humedad, viento — son placeholder) una vez confirmado lo
      anterior.
- [ ] Backend legacy para el CR10X real (solo validable en campo — mayor
      riesgo técnico según la propuesta CAV 2026/2027)

## Aylen — Driver LoRa (RFM95) + OTA por LoRa

Archivos: `components/common/inc|src/sx1276.*` (driver compartido),
`components/common/inc/lora_proto.h` (trama), `gateway/main/src/lora_rx.c`,
`ota_lora_gw.c`, `remote_image.c`, `remote/main/src/lora_tx.c`,
`ota_lora_node.c`. Detalle en `docs/LORA.md`.

- [x] Driver SX1276 (portado de `aylen27s/ota-by-lora`, con correcciones:
      bit de modo LoRa, base de FIFO TX, tamaño de transferencia SPI,
      overflows, flags de IRQ, race en RX, pulso de reset)
- [x] Driver único compartido en `components/common` (pines/SF/BW por Kconfig)
- [x] Formato de paquete versionado con id de nodo (`lora_proto.h`), ACK de
      telemetría con reintentos y descarte de duplicados
- [x] Protocolo OTA por LoRa con validación de bytes recibidos vs. totales,
      CRC32, retransmisión ante paquete inválido y reinicio de la transmisión
- [x] Gateway: descarga HTTPS de la imagen del remoto (`SIGGECA_REMOTE_OTA_URL`,
      mismo bundle de certificados que la OTA propia) a `lora_img`
- [x] Compila con ESP-IDF 6.0.2 (gateway y remoto)
- [ ] Bring-up del módulo RFM95 en devkit (banda 915 MHz): enlace entre dos
      devkits y medir RSSI/SNR
- [ ] OTA completa en banco: publicar un `.bin` del remoto, ver que el
      gateway lo baje, lo ofrezca y que el remoto actualice y confirme
- [ ] Probar el rollback: imagen del remoto con la radio rota a propósito →
      tiene que volver sola a la anterior a los 3 ciclos
- [ ] Mover RST a GPIO33 / DIO0 a GPIO32 en el PCB final (GPIO15 es strapping)
- [ ] Definir con el equipo dónde se publica el `.bin` del remoto (misma
      convención que `public_images/<site>.bin` del gateway)

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
