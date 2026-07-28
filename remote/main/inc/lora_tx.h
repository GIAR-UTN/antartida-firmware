/**
 * @file lora_tx.h
 * @brief Remote-node hook into the shared LoRa driver (TX / initiator role).
 *
 * STUB -- owner: teammate in charge of the LoRa driver (RFM95).
 *
 * The remote node always initiates: wake, read Campbell, send one packet,
 * sleep. No listen window, no clock sync with the gateway needed (see
 * gateway/main/lora_rx.h and the CAV 2026/2027 proposal sec. 6.5). Build
 * this against the same wire format / packet framing as lora_rx.h so both
 * ends agree without needing a shared header across the two ESP-IDF apps
 * (duplicate the struct there if needed, or promote both to a shared
 * components/common/include/lora_driver.h once the format is settled).
 */

#ifndef LORA_TX_H
#define LORA_TX_H

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t lora_tx_init(void);
esp_err_t lora_tx_send(const uint8_t *data, size_t len);

#endif // LORA_TX_H
