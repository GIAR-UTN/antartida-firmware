/**
 * @file lora_rx.c
 * @brief STUB. Owner: teammate in charge of the LoRa driver (RFM95).
 */
#include "lora_rx.h"
#include "esp_log.h"

static const char *TAG = "lora_rx_stub";

esp_err_t lora_rx_init(void)
{
    ESP_LOGW(TAG, "lora_rx_init(): STUB, no RFM95 wired yet");
    return ESP_OK;
}

bool lora_rx_poll(lora_rx_packet_t *out)
{
    (void)out;
    return false;
}
