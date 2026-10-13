/**
 * @file sx1276.h
 * @brief Driver for the Semtech SX1276 LoRa transceiver (HopeRF RFM95W module).
 *
 * Originally written by Aylen (github.com/aylen27s/ota-by-lora), reviewed and
 * fixed before being moved here (LoRa mode bit, TX FIFO base, SPI transfer
 * size, buffer overflows, IRQ flag clearing, RX race, reset pulse -- see the
 * FIX comments in sx1276.c).
 *
 * Shared by both apps: the gateway listens permanently (gateway/main/src/
 * lora_rx.c), the remote node transmits and opens a short RX window
 * (remote/main/src/lora_tx.c). Radio parameters and pins come from Kconfig
 * (menuconfig -> SiGGECA common configuration -> LoRa) so both nodes are
 * guaranteed to be built with the same SF/BW/sync word.
 *
 * Threading: the driver has no internal lock. Each node has exactly one
 * task that owns the radio (gateway: lora_rx task, remote: remote_fsm task);
 * don't call it from anywhere else.
 */

#ifndef SX1276_H
#define SX1276_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "sdkconfig.h"
#include "hal/gpio_types.h" // gpio_num_t

// ==================== REGISTROS SX1276 ====================
#define REG_FIFO                   0x00
#define REG_OP_MODE                0x01
#define REG_FRF_MSB                0x06
#define REG_FRF_MID                0x07
#define REG_FRF_LSB                0x08
#define REG_PA_CONFIG              0x09
#define REG_OCP                    0x0B
#define REG_LNA                    0x0C
#define REG_FIFO_ADDR_PTR          0x0D
#define REG_FIFO_TX_BASE_ADDR      0x0E
#define REG_FIFO_RX_BASE_ADDR      0x0F
#define REG_FIFO_RX_CURRENT_ADDR   0x10
#define REG_IRQ_FLAGS_MASK         0x11
#define REG_IRQ_FLAGS              0x12
#define REG_RX_NB_BYTES            0x13
#define REG_PKT_SNR_VALUE          0x19
#define REG_PKT_RSSI_VALUE         0x1A
#define REG_MODEM_CONFIG_1         0x1D
#define REG_MODEM_CONFIG_2         0x1E
#define REG_SYMB_TIMEOUT_LSB       0x1F
#define REG_PREAMBLE_MSB           0x20
#define REG_PREAMBLE_LSB           0x21
#define REG_PAYLOAD_LENGTH         0x22
#define REG_MAX_PAYLOAD_LENGTH     0x23
#define REG_MODEM_CONFIG_3         0x26
#define REG_SYNC_WORD              0x39
#define REG_DIO_MAPPING_1          0x40
#define REG_VERSION                0x42
#define REG_PA_DAC                 0x4D

// Bits de REG_OP_MODE
// FIX: LongRangeMode es el bit 7 (0x80). Antes se escribía 0x01, que dejaba
// al chip en modo FSK (y el bit 0 pisaba el campo de modo).
#define OP_MODE_LONG_RANGE         0x80 // 1: LoRa, 0: FSK/OOK. Solo se puede cambiar en SLEEP.
#define OP_MODE_MODE_MASK          0x07

/* Bits IRQ Flags */
#define IRQ_RX_TIMEOUT_MASK        0x80
#define IRQ_RX_DONE_MASK           0x40
#define IRQ_PAYLOAD_CRC_ERROR_MASK 0x20
#define IRQ_TX_DONE_MASK           0x08


// ==================== PINES (Kconfig) ====================
#define LORA_PIN_MISO   ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_MISO)
#define LORA_PIN_MOSI   ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_MOSI)
#define LORA_PIN_SCK    ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_SCK)
#define LORA_PIN_CS     ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_CS)
#define LORA_PIN_RST    ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_RST)
#define LORA_PIN_DIO0   ((gpio_num_t)CONFIG_SIGGECA_LORA_PIN_DIO0)   // IRQ de TxDone / RxDone

// ==================== PARÁMETROS SPI ====================
#define LORA_SPI_HOST               SPI2_HOST           // pines 19,23,18 por defecto
#define LORA_SPI_CLOCK_HZ           (5 * 1000 * 1000)   // 5 MHz, el SX1276 soporta hasta 10 MHz
#define LORA_MF_ID                  0x12                // REG_VERSION esperado para SX1276

// ==================== PARÁMETROS DE LORA ====================
#define LORA_RST_WAIT_TIME_LOW_US   200                 // Mínimo 100us (datasheet 7.2.2)
#define LORA_RST_WAIT_TIME_HIGH_MS  10                  // Mínimo 5ms

#define LORA_FREQUENCY_HZ       ((uint32_t)CONFIG_SIGGECA_LORA_FREQUENCY_HZ)
#define LORA_TX_POWER_DBM       CONFIG_SIGGECA_LORA_TX_POWER_DBM
#define LORA_PREAMBLE_LEN       8       // Cantidad de símbolos de preámbulo
#define LORA_SYNC_WORD          CONFIG_SIGGECA_LORA_SYNC_WORD
#define LORA_SF                 CONFIG_SIGGECA_LORA_SF

#if CONFIG_SIGGECA_LORA_BW_500
#define LORA_BW                 LORA_BW_500_kHz
#define LORA_BW_HZ              500000
#elif CONFIG_SIGGECA_LORA_BW_250
#define LORA_BW                 LORA_BW_250_kHz
#define LORA_BW_HZ              250000
#else
#define LORA_BW                 LORA_BW_125_kHz
#define LORA_BW_HZ              125000
#endif

// Payload máximo por paquete. A SF7/125kHz, 124 bytes son ~200 ms de aire,
// por debajo del dwell time de 400 ms de la banda 915 MHz (a SF9 o más un
// paquete lleno lo excede -- revisar contra la normativa si se sube el SF).
// Los buffers que se pasen a sx1276_receive_packet() deben tener al menos este tamaño.
#define LORA_MAX_PAYLOAD        124
#define HEADER_MODE             0       // 1: Implícito, 0: Explícito
#define CRC_MODE                1       // 1: Habilitado, 0: Deshabilitado

typedef enum {
    LORA_BW_7_kHz, //0
    LORA_BW_10_kHz,
    LORA_BW_15_kHz,
    LORA_BW_20_kHz,
    LORA_BW_31_kHz,
    LORA_BW_41_kHz,
    LORA_BW_62_kHz,
    LORA_BW_125_kHz,
    LORA_BW_250_kHz,
    LORA_BW_500_kHz, //9
} sx1276_bw_options;

typedef enum {
    LORA_MODE_SLEEP, //0
    LORA_MODE_STANDBY,
    LORA_MODE_FSTX,
    LORA_MODE_TX,
    LORA_MODE_FSRX,
    LORA_MODE_RX //5  (RX continuo)
} sx1276_modes;

typedef enum {
    /* Coding rate
        001 para 4/5
        010 para 4/6
        011 para 4/7
        100 para 4/8
    */
    CR_4_5 = 1,
    CR_4_6,
    CR_4_7,
    CR_4_8
} sx1276_coding_rate;

typedef enum {
    SF_6 = 6,   // Requiere header implícito y config extra; no soportado por este driver
    SF_7,
    SF_8,
    SF_9,
    SF_10,
    SF_11,
    SF_12,
} sx1276_sf;

/**
    Inicializa SPI, resetea el módulo, verifica el chip (REG_VERSION) y
    configura frecuencia, potencia y parámetros del modem.
    Debe llamarse una sola vez al arrancar, antes de transmitir o recibir.
 */
esp_err_t sx1276_init(void);

/*
    Envía un paquete de forma bloqueante. Espera hasta el tiempo en el aire
    del paquete + margen a que llegue la interrupción de TxDone. Al terminar
    el módulo queda en STANDBY (hay que volver a llamar a sx1276_start_receive()
    para escuchar).
    Devuelve ESP_OK si se transmitió, ESP_ERR_INVALID_SIZE si len es 0 o > LORA_MAX_PAYLOAD,
    ESP_ERR_TIMEOUT si no hubo IRQ a tiempo.
 */
esp_err_t sx1276_send_packet(const uint8_t *data, uint8_t len);

/*
    Pone el módulo en modo recepción continua. Llamar antes de sx1276_receive_packet()
    y después de cada sx1276_send_packet().
 */
void sx1276_start_receive(void);

/*
    Bloquea la tarea actual hasta que llegue un paquete (o hasta timeout_ms,
    0 = esperar indefinidamente). buf debe tener al menos LORA_MAX_PAYLOAD bytes.
    Al volver, buf contiene el payload y *len su longitud. rssi y snr son opcionales.
    Devuelve ESP_OK si llegó paquete válido (CRC ok), ESP_ERR_TIMEOUT si venció
    el timeout, ESP_ERR_INVALID_CRC si el paquete llegó corrupto,
    ESP_ERR_INVALID_SIZE si el paquete excede LORA_MAX_PAYLOAD.
    El módulo sigue en RX continuo al volver.
 */
esp_err_t sx1276_receive_packet(uint8_t *buf, uint8_t *len, int16_t *rssi, float *snr, uint32_t timeout_ms);

/* Pone el radio en SLEEP (~1 uA). El remoto lo usa entre ciclos. */
void sx1276_sleep(void);

/*
    Tiempo en el aire (ms, redondeado hacia arriba) de un paquete de payload_len
    bytes con la configuración actual (datasheet 4.1.1.7). Sirve para dimensionar
    timeouts de ACK que sigan siendo correctos si se cambia SF/BW en Kconfig.
 */
uint32_t sx1276_time_on_air_ms(uint8_t payload_len);

#endif // SX1276_H
