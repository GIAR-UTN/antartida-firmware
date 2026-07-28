/**
 * @file lora_tx.c
 * @brief STUB. Owner: teammate in charge of the LoRa driver (RFM95).
 */
#include "lora_tx.h"
#include "esp_log.h"

static const char *TAG = "lora_tx_stub";

esp_err_t lora_tx_init(void)
{
    ESP_LOGW(TAG, "lora_tx_init(): STUB, no RFM95 wired yet");
    return ESP_OK;
}

esp_err_t lora_tx_send(const uint8_t *data, size_t len)
{
    (void)data;
    ESP_LOGW(TAG, "lora_tx_send(): STUB, dropping %u bytes", (unsigned)len);
    return ESP_OK;
}
