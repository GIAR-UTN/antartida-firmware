/**
 * @file lora_rx.h
 * @brief Gateway side of the LoRa link (always-listening role).
 *
 * Per the CAV 2026/2027 proposal (sec. 6.5), the gateway never sleeps its
 * LoRa receiver or syncs clocks with the remote node: the remote initiates
 * every exchange. lora_rx_init() starts a task that owns the radio and:
 *   - answers each TELEMETRY frame with a TELEMETRY_ACK right away (the
 *     remote only listens for ~a few hundred ms after its TX), offering the
 *     remote image stored in lora_img when the node runs another version;
 *   - stores the reading in a ring buffer in RTC memory, which survives the
 *     esp_restart() at the end of every gateway cycle (not a power loss);
 *   - serves OTA sessions when a node sends OTA_REQUEST (ota_lora_gw.h).
 *
 * gateway_fsm drains the buffer to InfluxDB with lora_rx_flush_to_telemetry()
 * whenever wifi is up (the active window, and again right before OTA, so
 * what arrived during the OFF window isn't lost on the reboot that follows).
 *
 * Wire format: components/common/inc/lora_proto.h.
 */

#ifndef LORA_RX_H
#define LORA_RX_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LORA_RX_BUFFER_DEPTH    32      // readings kept while Starlink is off (~32 h at 1/h)

esp_err_t lora_rx_init(void);

/** Number of readings waiting to be sent to InfluxDB. */
size_t lora_rx_pending(void);

/**
 * Sends buffered readings to InfluxDB, oldest first, until the buffer is
 * empty or a POST fails (the rest stay buffered for the next window).
 * Call only with wifi connected. Returns how many were sent.
 */
int lora_rx_flush_to_telemetry(void);

#endif // LORA_RX_H
