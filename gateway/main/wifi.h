
/**
 * @file wifi.h
 * @brief Definition of wifi related functions.
 */

#ifndef WIFI_H
#define WIFI_H

// --- Generic includes ---
#include "common.h"
#include "node_config.h"
#if CONFIG_EXAMPLE_CONNECT_WIFI
#include "esp_wifi.h"
#endif
#include "protocol_examples_common.h"
#include "example_common_private.h"

// --- Project related includes ---

// NOTE: SSID/password used to be redefined here via #ifdef MARAMBIO/ISLA_VEGA,
// shadowing (and silently winning over) the CONFIG_EXAMPLE_WIFI_SSID/PASSWORD
// values set through menuconfig. wifi.c now reads NODE_WIFI_SSID/
// NODE_WIFI_PASSWORD from node_config.h (Kconfig) instead -- one source of
// truth per value.

// --- Function definitions ---
void custom_handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
void custom_handler_on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data);
void custom_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data);
void custom_handler_on_sta_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data);
void custom_wifi_start(void);
void custom_wifi_stop(void);
esp_err_t custom_wifi_sta_do_connect(wifi_config_t wifi_config, bool wait);
esp_err_t custom_wifi_sta_do_disconnect(void);
void custom_wifi_shutdown(void);
esp_err_t custom_wifi_connect(void);
esp_err_t print_all_ips_tcpip(void* ctx);
esp_err_t custom_connect(void);


bool wifi_is_connected(void);
esp_err_t wifi_connect_if_needed(void);
esp_err_t wifi_disconnect_if_connected(void);

esp_err_t wifi_init(void);


#endif // WIFI_H
