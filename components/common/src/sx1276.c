
#include "sx1276.h"
#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "sx1276";

// Margen sobre el tiempo en el aire calculado para esperar TxDone
#define LORA_TX_TIMEOUT_MARGIN_MS   500

static spi_device_handle_t s_spi;
// FIX: volatile porque se escribe desde la tarea y se lee desde la ISR.
static volatile TaskHandle_t s_waiting_task = NULL; // tarea a notificar desde la ISR de DIO0


static uint8_t sx1276_read_reg(uint8_t reg);
static void sx1276_write_reg(uint8_t reg, uint8_t value);
static void sx1276_set_lna_boost(bool boost_mode);
static void sx1276_reset_lora(void);
static void sx1276_set_cs(bool mode);
static esp_err_t sx1276_set_irq(gpio_num_t pin, gpio_int_type_t type, gpio_isr_t handlerisr);
static void sx1276_set_frequency(uint32_t freq_hz);
static void sx1276_set_mode(sx1276_modes lora_mode);
static void sx1276_set_packet_params(uint8_t header_mode, sx1276_sf sf, uint8_t crc_mode, sx1276_coding_rate cr);
static void sx1276_set_bw(sx1276_bw_options bw);
static void sx1276_read_fifo(uint8_t *buf, uint8_t len);
static void sx1276_write_fifo(const uint8_t *buf, uint8_t len);
static void sx1276_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes);

// ==================== ISR de DIO0 ====================
static void IRAM_ATTR dio0_isr_handler(void *arg)
{
    /*
        El módulo LoRa pone DIO0 en alto cuando finaliza la transmisión o la
        recepción (según REG_DIO_MAPPING_1). Si hay una tarea esperando, la
        notificamos para que se desbloquee.
    */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    TaskHandle_t waiting = s_waiting_task;
    if (waiting != NULL) {
        vTaskNotifyGiveFromISR(waiting, &xHigherPriorityTaskWoken);
    }

    /*
        Si la tarea notificada tiene mayor prioridad que la que estaba corriendo,
        forzamos el cambio de contexto al salir de la ISR.
    */
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}


esp_err_t sx1276_init(void)
{
    // CS: false -> nivel alto para desconectar al slave del bus.
    gpio_reset_pin(LORA_PIN_CS);
    gpio_set_direction(LORA_PIN_CS, GPIO_MODE_OUTPUT);
    sx1276_set_cs(false);

    // Reset: nivel bajo >100us, luego >5ms antes de usar el chip.
    sx1276_reset_lora();

    /* Configuración de SPI */
    spi_bus_config_t buscfg = {
        .miso_io_num = LORA_PIN_MISO,
        .mosi_io_num = LORA_PIN_MOSI,
        .sclk_io_num = LORA_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        // FIX: antes era LORA_MAX_PAYLOAD (124), pero las transacciones de FIFO
        // son len + 1 byte de dirección, así que un paquete de 124 bytes fallaba.
        .max_transfer_sz = 256 + 1,
    };

    esp_err_t err = spi_bus_initialize(LORA_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize fallo: %s", esp_err_to_name(err));
        return err;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LORA_SPI_CLOCK_HZ,    // Máximo 10MHz para SX1276
        .mode = 0,                              // CPOL=0, CPHA=0
        .spics_io_num = -1,                     // Control manual del CS
        .queue_size = 1,
    };

    err = spi_bus_add_device(LORA_SPI_HOST, &devcfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device fallo: %s", esp_err_to_name(err));
        return err;
    }

    // Prueba de conectividad (REG_VERSION = 0x12 en SX1276)
    uint8_t version = sx1276_read_reg(REG_VERSION);
    if (version != LORA_MF_ID) {
        ESP_LOGE(TAG, "No se detecto SX1276 (REG_VERSION=0x%02X). Revisar cableado.", version);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "SX1276 detectado (version 0x%02X)", version);

    /*
        FIX: el bit LongRangeMode solo se puede cambiar en SLEEP. Primero pasamos a
        SLEEP en FSK (modo actual tras reset), y recién ahí activamos LoRa.
        set_mode() siempre escribe el bit LoRa a partir de acá.
    */
    sx1276_write_reg(REG_OP_MODE, LORA_MODE_SLEEP);
    sx1276_set_mode(LORA_MODE_SLEEP);
    if ((sx1276_read_reg(REG_OP_MODE) & OP_MODE_LONG_RANGE) == 0) {
        ESP_LOGE(TAG, "No se pudo pasar el SX1276 a modo LoRa");
        return ESP_FAIL;
    }

    /*
        FIX: tras el reset REG_FIFO_TX_BASE_ADDR vale 0x80. El modem transmite desde
        TxBaseAddr, así que escribir el payload en 0x00 hacía que se enviara basura.
        Usamos toda la FIFO (256 bytes) para TX y para RX, ya que es half-duplex.
    */
    sx1276_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00);
    sx1276_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00);

    /* Configuraciones propias de la comunicación LoRa */
    sx1276_set_frequency(LORA_FREQUENCY_HZ);
    sx1276_set_bw(LORA_BW);
    sx1276_set_packet_params(HEADER_MODE, (sx1276_sf)LORA_SF, CRC_MODE, CR_4_5);

    // El propio radio descarta paquetes más largos que LORA_MAX_PAYLOAD
    sx1276_write_reg(REG_MAX_PAYLOAD_LENGTH, LORA_MAX_PAYLOAD);

    /* Preámbulo y sync word */
    sx1276_write_reg(REG_PREAMBLE_MSB, 0x00);
    sx1276_write_reg(REG_PREAMBLE_LSB, LORA_PREAMBLE_LEN);
    sx1276_write_reg(REG_SYNC_WORD, LORA_SYNC_WORD);

    // LNA sin boost de corriente para minimizar consumo.
    sx1276_set_lna_boost(false);

    // AGC automático (bit 2). LowDataRateOptimize (bit 3) es obligatorio cuando
    // el símbolo dura más de 16 ms (SF11/SF12 a 125 kHz, SF12 a 250 kHz).
    const uint32_t symbol_us = (1000000UL << LORA_SF) / LORA_BW_HZ;
    sx1276_write_reg(REG_MODEM_CONFIG_3, 0x04 | (symbol_us > 16000 ? 0x08 : 0x00));

    // Potencia de salida (PA_BOOST): Pout = 17 - (15 - OutputPower)
    uint8_t power = LORA_TX_POWER_DBM;
    if (power > 17) power = 17;
    if (power < 2) power = 2;
    sx1276_write_reg(REG_PA_CONFIG, 0x80 | (power - 2)); // 0x80 = PA_BOOST

    /* Prueba config para llegar a los 500m en ciudad: PA_BOOST 20dBm
    sx1276_write_reg(REG_PA_CONFIG, 0x80 | 0x70 | 0x0F);
    sx1276_write_reg(REG_PA_DAC, 0x87);
    sx1276_write_reg(REG_OCP, 0x20 | 0x12); // OcpOn, Imax = 150mA (ver tabla 6.4 datasheet)
    */

    // DIO0 como entrada de interrupción (flanco ascendente)
    err = sx1276_set_irq(LORA_PIN_DIO0, GPIO_INTR_POSEDGE, dio0_isr_handler);
    if (err != ESP_OK) {
        return err;
    }

    sx1276_set_mode(LORA_MODE_STANDBY);

    ESP_LOGI(TAG, "SX1276 inicializado a %lu Hz, SF%d, BW %lu kHz, potencia %d dBm",
             (unsigned long)LORA_FREQUENCY_HZ, LORA_SF, (unsigned long)(LORA_BW_HZ / 1000), power);

    return ESP_OK;
}

esp_err_t sx1276_send_packet(const uint8_t *data, uint8_t len)
{
    // FIX: sin este control, len > LORA_MAX_PAYLOAD desbordaba el buffer de write_fifo.
    if (data == NULL || len == 0 || len > LORA_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }

    // STANDBY: la FIFO no es accesible en SLEEP, y en RX el modem podría escribirla.
    sx1276_set_mode(LORA_MODE_STANDBY);

    // DIO0 -> TxDone (01 en bits 7:6)
    sx1276_write_reg(REG_DIO_MAPPING_1, 0x40);

    // FIFO apuntando al comienzo de la zona de TX
    sx1276_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    sx1276_write_fifo(data, len);

    // Cantidad de bytes a transmitir
    sx1276_write_reg(REG_PAYLOAD_LENGTH, len);

    // Limpio flags de IRQ previos (y con eso DIO0 vuelve a bajo)
    sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);

    // Registro esta tarea para que la ISR la despierte y descarto notificaciones viejas
    ulTaskNotifyValueClear(NULL, 0xFFFFFFFF);
    s_waiting_task = xTaskGetCurrentTaskHandle();

    sx1276_set_mode(LORA_MODE_TX);

    uint32_t tx_timeout_ms = sx1276_time_on_air_ms(len) + LORA_TX_TIMEOUT_MARGIN_MS;
    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tx_timeout_ms) + 1);
    s_waiting_task = NULL;

    uint8_t irq_flags = sx1276_read_reg(REG_IRQ_FLAGS);

    // FIX: en los caminos de error no se limpiaban los flags ni se salía de TX,
    // lo que dejaba DIO0 en alto y la próxima operación sin flanco de IRQ.
    sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx1276_set_mode(LORA_MODE_STANDBY);

    if (notified == 0) {
        ESP_LOGW(TAG, "Timeout esperando IRQ DIO0 TxDone");
        return ESP_ERR_TIMEOUT;
    }
    if ((irq_flags & IRQ_TX_DONE_MASK) == 0) {
        ESP_LOGW(TAG, "IRQ DIO0 sin flag TxDone (flags=0x%02X)", irq_flags);
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t sx1276_receive_packet(uint8_t *buf, uint8_t *len, int16_t *rssi, float *snr, uint32_t timeout_ms)
{
    if (buf == NULL || len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *len = 0;

    ulTaskNotifyValueClear(NULL, 0xFFFFFFFF);
    s_waiting_task = xTaskGetCurrentTaskHandle();

    /*
        FIX (race): si el paquete llegó antes de registrar la tarea, la ISR no
        notificó a nadie y DIO0 queda en alto hasta limpiar los flags, por lo que
        no habría otro flanco y la tarea quedaba bloqueada para siempre.
        Si DIO0 ya está en alto, no esperamos.
    */
    uint32_t notified = 1;
    if (gpio_get_level(LORA_PIN_DIO0) == 0) {
        TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
        notified = ulTaskNotifyTake(pdTRUE, ticks);
    }
    s_waiting_task = NULL;

    if (notified == 0) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t irq_flags = sx1276_read_reg(REG_IRQ_FLAGS);

    if ((irq_flags & IRQ_RX_DONE_MASK) == 0) {
        ESP_LOGW(TAG, "IRQ DIO0 sin flag RxDone (flags=0x%02X)", irq_flags);
        sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);
        return ESP_ERR_INVALID_STATE;
    }

    if (irq_flags & IRQ_PAYLOAD_CRC_ERROR_MASK) {
        // FIX: antes se retornaba sin limpiar flags -> DIO0 quedaba en alto.
        sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);
        ESP_LOGW(TAG, "RxDone con CRC invalido. Paquete descartado.");
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t nb_bytes = sx1276_read_reg(REG_RX_NB_BYTES);
    // FIX: nb_bytes puede valer hasta 255; sin este control se desbordaban
    // los buffers de read_fifo y el buffer del usuario.
    if (nb_bytes == 0 || nb_bytes > LORA_MAX_PAYLOAD) {
        sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);
        ESP_LOGW(TAG, "Paquete de %u bytes fuera de rango. Descartado.", nb_bytes);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t current_addr = sx1276_read_reg(REG_FIFO_RX_CURRENT_ADDR);
    sx1276_write_reg(REG_FIFO_ADDR_PTR, current_addr);
    sx1276_read_fifo(buf, nb_bytes);
    *len = nb_bytes;

    int8_t raw_snr = (int8_t)sx1276_read_reg(REG_PKT_SNR_VALUE);
    if (snr != NULL) {
        *snr = raw_snr / 4.0f;
    }
    if (rssi != NULL) {
        // Datasheet 5.5.5 (puerto HF): RSSI = -157 + PacketRssi; si SNR < 0 se suma SNR
        int16_t r = (int16_t)sx1276_read_reg(REG_PKT_RSSI_VALUE) - 157;
        if (raw_snr < 0) {
            r += raw_snr / 4;
        }
        *rssi = r;
    }

    sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);

    return ESP_OK;
}

void sx1276_sleep(void)
{
    sx1276_set_mode(LORA_MODE_SLEEP);
}

uint32_t sx1276_time_on_air_ms(uint8_t payload_len)
{
    // Datasheet SX1276, sección 4.1.1.7. Header explícito, CRC on, CR 4/5.
    const int sf = LORA_SF;
    const uint32_t symbol_us = (1000000UL << sf) / LORA_BW_HZ;
    const int de = symbol_us > 16000 ? 1 : 0;   // LowDataRateOptimize
    const int ih = HEADER_MODE;
    const int crc = CRC_MODE;
    const int cr = 1;                            // CR_4_5

    int32_t num = 8 * payload_len - 4 * sf + 28 + 16 * crc - 20 * ih;
    int32_t den = 4 * (sf - 2 * de);
    int32_t n_payload = 8;
    if (num > 0) {
        n_payload += ((num + den - 1) / den) * (cr + 4);
    }
    // Preámbulo: (n_preamble + 4.25) símbolos
    uint64_t total_us = (uint64_t)symbol_us * (LORA_PREAMBLE_LEN * 4 + 17) / 4
                      + (uint64_t)symbol_us * n_payload;
    return (uint32_t)((total_us + 999) / 1000);
}

void sx1276_start_receive(void)
{
    sx1276_set_mode(LORA_MODE_STANDBY);
    // DIO0 -> RxDone (00 en bits 7:6)
    sx1276_write_reg(REG_DIO_MAPPING_1, 0x00);
    sx1276_write_reg(REG_FIFO_ADDR_PTR, 0x00);
    sx1276_write_reg(REG_IRQ_FLAGS, 0xFF);
    sx1276_set_mode(LORA_MODE_RX);
}


// ======= Funciones estáticas =======
static void sx1276_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes)
{
    spi_transaction_t t = {
        .length = len_bytes * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    sx1276_set_cs(true);
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    sx1276_set_cs(false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error SPI: %s", esp_err_to_name(err));
    }
}

static void sx1276_write_fifo(const uint8_t *buf, uint8_t len)
{
    uint8_t tx[LORA_MAX_PAYLOAD + 1];
    if (len > LORA_MAX_PAYLOAD) {
        len = LORA_MAX_PAYLOAD;
    }
    tx[0] = REG_FIFO | 0x80;
    memcpy(&tx[1], buf, len);
    sx1276_spi_transfer(tx, NULL, (size_t)len + 1);
}

static void sx1276_read_fifo(uint8_t *buf, uint8_t len)
{
    uint8_t tx[LORA_MAX_PAYLOAD + 1] = { 0 };
    uint8_t rx[LORA_MAX_PAYLOAD + 1] = { 0 };
    if (len > LORA_MAX_PAYLOAD) {
        len = LORA_MAX_PAYLOAD;
    }
    tx[0] = REG_FIFO & 0x7F;
    sx1276_spi_transfer(tx, rx, (size_t)len + 1);
    memcpy(buf, &rx[1], len);
}

static uint8_t sx1276_read_reg(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx[2] = { 0 };
    sx1276_spi_transfer(tx, rx, sizeof(tx));
    return rx[1];
}

static void sx1276_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), value };
    sx1276_spi_transfer(tx, NULL, sizeof(tx));
}


static void sx1276_set_lna_boost(bool boost_mode)
{
    /*
        LNA: Low Noise Amplifier.
        Bits 1:0 --> 11 Boost on (150% corriente LNA), 00 por defecto.
    */
    uint8_t lna_reg_value = sx1276_read_reg(REG_LNA);
    uint8_t value = boost_mode ? (lna_reg_value | 0x03) : (lna_reg_value & ~(0x03));
    sx1276_write_reg(REG_LNA, value);
}


static void sx1276_reset_lora(void)
{
    gpio_reset_pin(LORA_PIN_RST);
    gpio_set_direction(LORA_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LORA_PIN_RST, 0);
    // FIX: pdMS_TO_TICKS(1) da 0 ticks con CONFIG_FREERTOS_HZ=100 (default),
    // así que el pulso de reset podía durar ~0. Usamos espera activa en us.
    esp_rom_delay_us(LORA_RST_WAIT_TIME_LOW_US);
    gpio_set_level(LORA_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(LORA_RST_WAIT_TIME_HIGH_MS) + 1);
}

static void sx1276_set_cs(bool mode)
{
    gpio_set_level(LORA_PIN_CS, mode ? 0 : 1);
}


static esp_err_t sx1276_set_irq(gpio_num_t pin, gpio_int_type_t type, gpio_isr_t handlerisr)
{
    // FIX: antes los parámetros se ignoraban y se usaban constantes.
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_intr_type(pin, type);

    // ESP_ERR_INVALID_STATE = el servicio ya estaba instalado (ok)
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service fallo: %s", esp_err_to_name(err));
        return err;
    }
    err = gpio_isr_handler_add(pin, handlerisr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_isr_handler_add fallo: %s", esp_err_to_name(err));
    }
    return err;
}

static void sx1276_set_frequency(uint32_t freq_hz)
{
    // Frf = freq_hz * 2^19 / 32e6
    uint64_t frf = ((uint64_t)freq_hz << 19) / 32000000ULL;
    sx1276_write_reg(REG_FRF_MSB, (uint8_t)(frf >> 16));
    sx1276_write_reg(REG_FRF_MID, (uint8_t)(frf >> 8));
    sx1276_write_reg(REG_FRF_LSB, (uint8_t)(frf >> 0));
}

static void sx1276_set_mode(sx1276_modes lora_mode)
{
    // FIX: LongRangeMode es el bit 7. LowFrequencyModeOn (bit 3) queda en 0 porque
    // trabajamos en 915 MHz (puerto HF).
    sx1276_write_reg(REG_OP_MODE, OP_MODE_LONG_RANGE | ((uint8_t)lora_mode & OP_MODE_MODE_MASK));
}

static void sx1276_set_bw(sx1276_bw_options bw)
{
    uint8_t value = sx1276_read_reg(REG_MODEM_CONFIG_1);
    value = (value & 0x0F) | (uint8_t)(bw << 4);
    sx1276_write_reg(REG_MODEM_CONFIG_1, value);
}

static void sx1276_set_packet_params(uint8_t header_mode, sx1276_sf sf, uint8_t crc_mode, sx1276_coding_rate cr)
{
    // MODEM_CONFIG_1: [7:4] BW, [3:1] CR, [0] ImplicitHeaderModeOn
    uint8_t value = sx1276_read_reg(REG_MODEM_CONFIG_1) & 0xF0;
    value = value | (uint8_t)(cr << 1) | (header_mode & 0x01);
    sx1276_write_reg(REG_MODEM_CONFIG_1, value);

    // MODEM_CONFIG_2: [7:4] SF, [3] TxContinuous, [2] RxPayloadCrcOn, [1:0] SymbTimeout MSB
    value = sx1276_read_reg(REG_MODEM_CONFIG_2) & 0x0B;
    value = value | (uint8_t)(sf << 4) | (uint8_t)((crc_mode & 0x01) << 2);
    sx1276_write_reg(REG_MODEM_CONFIG_2, value);
}
