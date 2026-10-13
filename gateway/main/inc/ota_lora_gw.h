/**
 * @file ota_lora_gw.h
 * @brief Gateway side of the OTA-over-LoRa protocol (sender).
 *
 * Protocol by Aylen (github.com/aylen27s/ota-by-lora), adapted to the
 * remote-initiated scheme: the gateway never pushes an image on its own. It
 * advertises the version it holds in every TELEMETRY_ACK, and runs a
 * session only when a remote node answers with OTA_REQUEST (see
 * lora_proto.h, docs/LORA.md).
 *
 * The image comes from the "lora_img" data partition, filled by
 * remote_image_fetch() during the gateway's own OTA window.
 *
 * All functions except the image lock helpers must be called from the
 * lora_rx task, which owns the radio.
 */

#ifndef OTA_LORA_GW_H
#define OTA_LORA_GW_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"
#include "lora_proto.h"

#define OTA_LORA_IMG_PARTITION  "lora_img"

/** Finds the partition and loads/validates the stored image (if any). */
esp_err_t ota_lora_gw_init(void);

/** Re-reads the stored image after remote_image_fetch() rewrote it. */
esp_err_t ota_lora_gw_reload(void);

/** Marks the stored image as unusable (called before rewriting it). */
void ota_lora_gw_invalidate(void);

/** The lora_img partition, or NULL if the partition table lacks it. */
const esp_partition_t *ota_lora_gw_partition(void);

/** Version string of the stored image; false if there's no valid image. */
bool ota_lora_gw_image_version(char out[LORA_FW_VERSION_LEN + 1]);

/**
 * Whether to offer the stored image to a node running remote_version.
 * Fills offer_version when it returns true.
 */
bool ota_lora_gw_should_offer(const char *remote_version, char offer_version[LORA_FW_VERSION_LEN]);

/**
 * Runs a whole session with node_id (blocking, tens of minutes).
 * Called by the lora_rx task when an OTA_REQUEST arrives.
 */
esp_err_t ota_lora_gw_serve(uint8_t node_id, const char *requested_version);

/** True while a session is in progress. gateway_fsm postpones its own
 *  OTA/reboot while this is true. */
bool ota_lora_gw_busy(void);

/**
 * Stops offering the image / accepting OTA_REQUESTs (true) or resumes (false).
 * gateway_fsm holds offers once it's past the point where it waits for a
 * session, so a new one can't start right before the gateway reboots.
 * Offers are enabled again on the next boot.
 */
void ota_lora_gw_hold_offers(bool hold);

/** Exclusive access to the lora_img partition (session vs. download). */
bool ota_lora_gw_lock(uint32_t timeout_ms);
void ota_lora_gw_unlock(void);

#endif // OTA_LORA_GW_H
