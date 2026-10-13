/**
 * @file lora_tx.h
 * @brief Remote-node side of the LoRa link (initiator role).
 *
 * The remote node always initiates: wake, read Campbell, send one
 * TELEMETRY frame, listen briefly for the gateway's TELEMETRY_ACK, sleep.
 * No clock sync with the gateway needed (proposal sec. 6.5). The ACK also
 * carries the gateway's OTA offer for this node -- see ota_lora_node.h.
 *
 * Wire format: components/common/inc/lora_proto.h (shared with the gateway).
 * Radio driver: components/common/inc/sx1276.h. This task (remote_fsm) is the
 * only owner of the radio on this node.
 */

#ifndef LORA_TX_H
#define LORA_TX_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "lora_proto.h"

#define LORA_TX_ATTEMPTS    3   // telemetry retries if no ACK arrives

typedef struct {
    bool    ota_offer;                              // gateway has an image for us
    char    ota_version[LORA_FW_VERSION_LEN + 1];   // NUL-terminated
    int16_t rssi_dbm;                               // of the ACK, as heard here
    float   snr_db;
} lora_tx_ack_t;

esp_err_t lora_tx_init(void);

/** This node's LoRa id (CONFIG_SIGGECA_LORA_NODE_ID). */
uint8_t lora_tx_node_id(void);

/**
 * Sends msg (header and fw_version are filled in here) and waits for the
 * gateway's ACK, retrying up to LORA_TX_ATTEMPTS times.
 * @return ESP_OK only if the gateway ACKed (ack filled in),
 *         ESP_ERR_TIMEOUT if no ACK after all attempts.
 */
esp_err_t lora_tx_send_telemetry(lora_msg_telemetry_t *msg, lora_tx_ack_t *ack);

/** Puts the radio to sleep until the next cycle. */
void lora_tx_sleep(void);

#endif // LORA_TX_H
