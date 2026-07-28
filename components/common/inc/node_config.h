/**
 * @file node_config.h
 * @brief Single source of truth for site/node-specific configuration.
 *
 * Every value here used to be a scattered `#ifdef MARAMBIO / ISLA_VEGA`
 * spread across common.h, gpio.h, wifi.h, victron_ble.h, victron_crypto.h
 * and metrics_task.h in the CAV 2025/2026 firmware. That duplication is how
 * the deployed antenna cycle (5 min ON / 1 h OFF) ended up silently
 * different from what the CAV report documents (15 min every 6 h).
 *
 * Site selection and all site-specific values are now Kconfig options
 * (see ../Kconfig) so `idf.py menuconfig` -- or a checked-in
 * sdkconfig.defaults.<site> -- is the only place that needs to change.
 */

#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include "sdkconfig.h"

#if CONFIG_SIGGECA_SITE_MARAMBIO
    #define SITE_LOCATION_TAG "MARAMBIO"
#elif CONFIG_SIGGECA_SITE_ISLA_VEGA
    #define SITE_LOCATION_TAG "ISLAVEGA"
#else
    #define SITE_LOCATION_TAG "UNKNOWN"
#endif

// Gateway and remote node can share the same SIGGECA_SITE (e.g. both at
// Isla Vega/Bahia del Diablo), so this second tag is what actually tells
// them apart in InfluxDB/Grafana. Each app pins it via its own
// sdkconfig.defaults -- see gateway/sdkconfig.defaults, remote/sdkconfig.defaults.
#if CONFIG_SIGGECA_NODE_ROLE_REMOTE
    #define NODE_ROLE_TAG "REMOTE"
#else
    #define NODE_ROLE_TAG "GATEWAY"
#endif

// --- WiFi (gateway only) ---
#define NODE_WIFI_SSID              CONFIG_SIGGECA_WIFI_SSID
#define NODE_WIFI_PASSWORD          CONFIG_SIGGECA_WIFI_PASSWORD

// --- Antenna duty cycle (gateway only) ---
#define NODE_RELAY_ON_MS            CONFIG_SIGGECA_RELAY_ON_MS
#define NODE_RELAY_OFF_MS           CONFIG_SIGGECA_RELAY_OFF_MS

// --- Victron BLE MPPT (gateway only) ---
#define NODE_VICTRON_MPPT_MAC       CONFIG_SIGGECA_VICTRON_MPPT_MAC
#define NODE_VICTRON_DEVICE_KEY     CONFIG_SIGGECA_VICTRON_DEVICE_KEY

// --- Telemetry / InfluxDB (both nodes, via gateway relay) ---
#define NODE_INFLUX_URL             CONFIG_SIGGECA_INFLUX_URL
#define NODE_INFLUX_ORG             CONFIG_SIGGECA_INFLUX_ORG
#define NODE_INFLUX_BUCKET          CONFIG_SIGGECA_INFLUX_BUCKET
#define NODE_INFLUX_TOKEN           CONFIG_SIGGECA_INFLUX_TOKEN

// --- OTA ---
#define NODE_OTA_URL                CONFIG_SIGGECA_OTA_URL

#endif // NODE_CONFIG_H
