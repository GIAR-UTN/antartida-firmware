/**
 * @file ota_lora_node.h
 * @brief Remote-node side of the OTA-over-LoRa protocol (receiver).
 *
 * Protocol by Aylen (github.com/aylen27s/ota-by-lora), adapted to the
 * remote-initiated scheme of this system: the remote only listens right
 * after its hourly TELEMETRY, so the OTA starts when the gateway's
 * TELEMETRY_ACK offers a version and the remote decides to ask for it
 * (see lora_proto.h and docs/LORA.md).
 *
 * The new image boots in ESP_OTA_IMG_PENDING_VERIFY state and is only
 * confirmed by remote_fsm once it gets a real ACK from the gateway; see
 * CONFIG_SIGGECA_REMOTE_ROLLBACK_AFTER_FAILS.
 */

#ifndef OTA_LORA_NODE_H
#define OTA_LORA_NODE_H

#include <stdbool.h>
#include "esp_err.h"

/** False if version is the running one or the last one that rolled back. */
bool ota_lora_node_wants(const char *version);

/**
 * Asks the gateway for the image and receives it. Blocking (~15 min for a
 * ~300 KB image at SF7/125 kHz), kicks the HW watchdog on every packet.
 * On success it reboots into the new image and never returns.
 * @return error if the session couldn't start or was aborted; the caller
 *         just goes back to its normal cycle and retries on a later offer.
 */
esp_err_t ota_lora_node_run(const char *version);

#endif // OTA_LORA_NODE_H
