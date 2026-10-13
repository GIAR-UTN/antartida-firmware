/**
 * @file remote_image.h
 * @brief Downloads the remote node's firmware image so the gateway can relay
 * it over LoRa (see ota_lora_gw.h).
 *
 * Runs from gateway_fsm during the OTA window, right before the gateway's
 * own execute_ota(), when Starlink/wifi are already up. Uses the same
 * esp_crt_bundle as the gateway's own OTA, so the server's certificate is
 * verified against the ESP-IDF CA bundle.
 */

#ifndef REMOTE_IMAGE_H
#define REMOTE_IMAGE_H

#include "esp_err.h"

/**
 * Downloads CONFIG_SIGGECA_REMOTE_OTA_URL into the lora_img partition if
 * the server has a different version than the one already stored.
 * @return ESP_OK if the stored image is now up to date (downloaded or
 *         already there), ESP_ERR_NOT_SUPPORTED if no URL is configured,
 *         other errors if the download/validation failed (the partition is
 *         then left without a valid image, so nothing gets offered).
 */
esp_err_t remote_image_fetch(void);

#endif // REMOTE_IMAGE_H
