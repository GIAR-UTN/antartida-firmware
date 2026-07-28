/**
 * @file ota.h
 * @brief Definition of all Over-The-Air related functions.
 */

#ifndef OTA_H
#define OTA_H

// --- Generic includes ---
#include "common.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "uart_console.h"
#include "wdt_sw.h"
#include "node_config.h"

#define OTA_RETRY_TIMEOUT_MS    30*1000

// NOTE: the CAV 2025/2026 firmware redefined CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL
// here via #ifdef MARAMBIO/ISLA_VEGA, on top of the Kconfig-provided value of
// the same name -- two sources of truth for one setting. The URL now comes
// only from Kconfig (NODE_OTA_URL, see node_config.h / ../Kconfig).

void execute_ota(void);
esp_err_t _http_event_handler(esp_http_client_event_t *evt);

#endif // OTA_H