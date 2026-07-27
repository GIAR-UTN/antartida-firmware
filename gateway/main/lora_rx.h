/**
 * @file lora_rx.h
 * @brief Gateway-side hook into the shared LoRa driver (RX / always-listening role).
 *
 * STUB -- owner: teammate in charge of the LoRa driver (RFM95).
 *
 * Per the CAV 2026/2027 proposal (sec. 6.5), the gateway never needs to
 * sleep its LoRa receiver or synchronize clocks with the remote node: the
 * remote initiates every exchange, and the gateway just needs to always be
 * listening and hand off whatever arrives to telemetry. Implement this
 * against a single shared `lora_driver.h`/.c (init/send/on_receive) so the
 * remote node's TX path and this RX path use the exact same wire format --
 * see the plan's "Contratos de driver" section for why it should be one
 * driver instead of two.
 *
 * Until the real LoRa driver exists, gateway_fsm calls lora_rx_poll() and
 * gets "no packet available", so the FSM can be developed/tested without
 * hardware.
 */

#ifndef LORA_RX_H
#define LORA_RX_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint8_t data[64];
    size_t len;
} lora_rx_packet_t;

esp_err_t lora_rx_init(void);

/** Non-blocking. Returns true and fills *out if a packet from the remote
 *  node arrived since the last call, false otherwise. */
bool lora_rx_poll(lora_rx_packet_t *out);

#endif // LORA_RX_H
