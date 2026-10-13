# Enlace LoRa y OTA del nodo remoto

Driver, trama y OTA por LoRa entre el nodo remoto (Bahía del Diablo) y el
gateway (refugio). El driver SX1276 y el protocolo de OTA son de Aylen
([aylen27s/ota-by-lora](https://github.com/aylen27s/ota-by-lora)), corregidos
y adaptados al esquema asimétrico de la propuesta (sec. 6.5): **el remoto
siempre inicia, el gateway siempre escucha**.

| Archivo | Qué hace |
|---|---|
| `components/common/{inc,src}/sx1276.*` | Driver del radio (RFM95 = SX1276). Pines y parámetros por Kconfig |
| `components/common/inc/lora_proto.h` | Formato de trama compartido por ambos nodos |
| `remote/main/src/lora_tx.c` | TX de telemetría + espera del ACK |
| `remote/main/src/ota_lora_node.c` | Recepción de la imagen (`esp_ota_*`) |
| `gateway/main/src/lora_rx.c` | Tarea que escucha siempre, ACK, buffer en RTC, envío a InfluxDB |
| `gateway/main/src/ota_lora_gw.c` | Envío de la imagen desde la partición `lora_img` |
| `gateway/main/src/remote_image.c` | Descarga HTTPS de la imagen del remoto a `lora_img` |

## Configuración

`idf.py menuconfig` → *SiGGECA common configuration* → *LoRa*: frecuencia
(915 MHz), SF (7 por defecto), BW (125 kHz), potencia (17 dBm, PA_BOOST),
sync word (0x12), pines e id del nodo remoto. **SF, BW y sync word tienen que
coincidir en los dos nodos**: como viven en el componente común, alcanza con
no tocarlos en una sola de las dos apps.

En el gateway, *OTA* → `SIGGECA_REMOTE_OTA_URL`: URL HTTPS del `.bin` del
remoto (vacía = OTA por LoRa deshabilitada).

## Trama

Todas empiezan con `lora_hdr_t` (7 bytes: magic `0xA7`, versión de
protocolo, tipo, origen, destino, seq). El gateway es el id 0. Se descarta todo
lo que no tenga el magic, la versión o el destino correctos.

### Telemetría (una vez por ciclo del remoto)

```
remoto                                   gateway
TELEMETRY (seq, fw, Campbell, BME280) ->
                                     <-  TELEMETRY_ACK (seq, ¿oferta OTA?)
```

- El remoto escucha solo una ventana corta tras su TX (tiempo en el aire del
  ACK + 300 ms, ~380 ms a SF7). Si no llega el ACK reintenta hasta 3 veces con
  el mismo seq y backoff aleatorio.
- El gateway contesta **antes** de guardar o loguear nada, y descarta
  duplicados (mismo nodo y mismo seq) pero los vuelve a confirmar.
- Si el Campbell falla, la trama sale igual con `CAMPBELL_OK` en 0
  (heartbeat). Así el gateway ve que el nodo está vivo y una OTA puede
  llegar a arreglar justamente el driver del Campbell.
- El gateway guarda las lecturas en un ring buffer de 32 en memoria RTC
  (`RTC_NOINIT`). Sobrevive al `esp_restart()` de cada ciclo del gateway,
  pero no a un corte de energía. Se vacía hacia InfluxDB (`remote_weather`,
  tags `loc`, `role=REMOTE`, `node`) cuando hay wifi: al inicio de la ventana
  activa y de nuevo antes de la OTA.

### OTA del remoto

```
remoto                                   gateway
TELEMETRY                            ->
                                     <-  TELEMETRY_ACK (oferta: versión X)
OTA_REQUEST (X)                      ->
                                     <-  OTA_START (sesión, tamaño, CRC32, chunks)
OTA_ACK (next=0)                     ->
                                     <-  OTA_DATA (chunk 0, 115 bytes)
OTA_ACK (next=1)                     ->
...
                                     <-  OTA_END
OTA_ACK                              ->  remoto valida y reinicia
```

- El gateway ofrece la imagen de `lora_img` cuando su versión es distinta de
  la que reporta el remoto. El remoto la pide solo si no es la que corre ni la
  última que hizo rollback, y si su propia imagen ya está confirmada.
- Stop-and-wait. Si llega un paquete con CRC LoRa inválido, el remoto pide
  retransmisión al instante. Un duplicado (ACK perdido) se vuelve a confirmar
  sin escribirse. Si llega un chunk fuera de orden, el NACK indica desde
  dónde seguir. Un chunk que falla más de 8 veces genera un ABORT, y el remoto
  lo vuelve a pedir en una oferta posterior.
- En `OTA_END` el remoto valida **bytes recibidos == tamaño anunciado**, el
  CRC32 de toda la imagen y `esp_ota_end()` (cabeceras + SHA-256), y recién
  ahí cambia la partición de boot.
- El remoto aborta la sesión si pasan 30 s sin paquetes.

### Duración

Cada chunk lleva ~205 ms de aire a SF7/125 kHz más el ACK (~20 ms) y el
turnaround. Una imagen de ~300 KB son ~2700 chunks, unos 12–15 min. A SF12 lo
mismo tarda varias horas.

## Puntos de falla y cómo se cubren

| Riesgo | Mitigación |
|---|---|
| El gateway reinicia en cada ciclo (`execute_ota()` → `esp_restart()`) y cortaría una sesión | Al llegar a `ANTENNA_OFF` el gateway deja de ofrecer (para que no arranque una sesión justo antes del reinicio) y posterga su OTA mientras `ota_lora_gw_busy()`, hasta `SIGGECA_LORA_OTA_MAX_DEFER_MIN` (90 min) |
| Una imagen nueva rompe la radio y el remoto queda inalcanzable | Rollback habilitado en el remoto. La imagen nueva se confirma recién con el primer ACK real del gateway. Si en `SIGGECA_REMOTE_ROLLBACK_AFTER_FAILS` ciclos (3) no lo consigue, vuelve a la anterior. El primer ciclo tras la OTA corre enseguida, sin esperar 1 h |
| Una imagen nueva cuelga o crashea al arrancar | Rollback del bootloader (reinicia sin haberse marcado válida) |
| La sesión dura decenas de minutos dentro de un mismo estado del FSM | `wdt_hw_kick()` en cada paquete |
| Una descarga del gateway a medias deja basura en `lora_img` | `esp_image_verify()` tras escribir. Si falla, no se ofrece nada. Un mutex impide reescribir la partición durante una sesión |
| Se ofrece una imagen que no entra en el remoto | Los slots OTA del remoto y `lora_img` miden lo mismo (896 KB): si la imagen no entra, el build del remoto falla |
| La telemetría de la ventana OFF se pierde en el reinicio del gateway | Buffer en RTC + vaciado antes de la OTA |

**Contra conocida del rollback por ACK:** si el gateway está caído justo
después de actualizar el remoto (3 ciclos = ~3 h), el remoto revierte una
imagen que estaba bien. Se recupera solo: en la próxima oferta la vuelve a
pedir, salvo que esa versión haya quedado marcada como inválida. En ese caso
hay que publicar una versión nueva.

## Pendiente

- Mover RST a GPIO33 y DIO0 a GPIO32 en el PCB final (GPIO15 es strapping).
- Bring-up en banco: enlace entre dos devkits con RFM95 y OTA completa.
- Timestamps: el gateway no tiene reloj (no hay SNTP). Las lecturas se
  marcan con el reloj del Campbell si parece válido (> 2020) y si no, con la
  hora de escritura en InfluxDB (con `rx_age_s` como referencia).
- Autenticación de la imagen: el CRC32 y el SHA-256 detectan corrupción,
  no manipulación. Si hace falta, usar Secure Boot v2 / firma de app.
- Dwell time: a SF ≥ 9 un paquete lleno supera los 400 ms. Revisar la
  normativa ENACOM antes de subir el SF.
