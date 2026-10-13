/**
 * @file lora_tx.c
 * @brief See lora_tx.h.
 */
#include "lora_tx.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "sdkconfig.h"
#include "sx1276.h"

static const char *TAG = "lora_tx";

// Random start so a reboot doesn't look like a duplicate to the gateway.
static uint16_t s_seq;

uint8_t lora_tx_node_id(void)
{
    return (uint8_t)CONFIG_SIGGECA_LORA_NODE_ID;
}

esp_err_t lora_tx_init(void)
{
    esp_err_t err = sx1276_init();
    if (err != ESP_OK) {
        return err;
    }
    s_seq = (uint16_t)(esp_random() & 0xFFFF);
    sx1276_sleep();
    ESP_LOGI(TAG, "LoRa node id %u ready", lora_tx_node_id());
    return ESP_OK;
}

void lora_tx_sleep(void)
{
    sx1276_sleep();
}

// Listens for a TELEMETRY_ACK echoing seq. Other frames are ignored.
static bool wait_ack(uint16_t seq, lora_tx_ack_t *ack)
{
    uint8_t buf[LORA_MAX_PAYLOAD];
    uint8_t len;
    int16_t rssi;
    float snr;
    const uint32_t window_ms = lora_reply_window_ms(sizeof(lora_msg_telemetry_ack_t));
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(window_ms);

    sx1276_start_receive();
    while (1) {
        int32_t left = (int32_t)(deadline - xTaskGetTickCount());
        if (left <= 0) {
            return false;
        }
        uint32_t left_ms = (uint32_t)left * portTICK_PERIOD_MS;
        esp_err_t err = sx1276_receive_packet(buf, &len, &rssi, &snr, left_ms ? left_ms : 1);
        if (err == ESP_ERR_TIMEOUT) {
            return false;
        }
        lora_hdr_t hdr;
        if (err != ESP_OK || !lora_hdr_is_for(buf, len, lora_tx_node_id(), &hdr) ||
            hdr.type != LORA_MSG_TELEMETRY_ACK || hdr.seq != seq ||
            len < sizeof(lora_msg_telemetry_ack_t)) {
            continue;
        }
        lora_msg_telemetry_ack_t msg;
        memcpy(&msg, buf, sizeof(msg));
        ack->ota_offer = msg.ota_offer != 0;
        memcpy(ack->ota_version, msg.ota_version, LORA_FW_VERSION_LEN);
        ack->ota_version[LORA_FW_VERSION_LEN] = '\0';
        ack->rssi_dbm = rssi;
        ack->snr_db = snr;
        return true;
    }
}

esp_err_t lora_tx_send_telemetry(lora_msg_telemetry_t *msg, lora_tx_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    const uint16_t seq = s_seq++;
    lora_hdr_fill(&msg->hdr, LORA_MSG_TELEMETRY, lora_tx_node_id(), LORA_GATEWAY_ID, seq);
    strncpy(msg->fw_version, esp_app_get_description()->version, LORA_FW_VERSION_LEN);

    for (int attempt = 1; attempt <= LORA_TX_ATTEMPTS; attempt++) {
        // Same seq on every retry: the gateway uses it to drop duplicates when
        // only our copy of the ACK got lost.
        esp_err_t err = sx1276_send_packet((const uint8_t *)msg, sizeof(*msg));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TX failed (%s), attempt %d/%d", esp_err_to_name(err), attempt, LORA_TX_ATTEMPTS);
        } else if (wait_ack(seq, ack)) {
            ESP_LOGI(TAG, "Telemetry seq %u ACKed (RSSI %d dBm, SNR %.1f dB)%s%s",
                     seq, ack->rssi_dbm, ack->snr_db,
                     ack->ota_offer ? ", OTA offered: " : "", ack->ota_offer ? ack->ota_version : "");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "No ACK for seq %u, attempt %d/%d", seq, attempt, LORA_TX_ATTEMPTS);
        }
        // Random backoff so we don't collide again with whatever caused the loss.
        vTaskDelay(pdMS_TO_TICKS(200 + (esp_random() % 800)));
    }
    return ESP_ERR_TIMEOUT;
}
