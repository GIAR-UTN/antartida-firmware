/**
 * @file main.h
 * @brief Definition main application functions and defines.
 */

#ifndef MAIN_H
#define MAIN_H

// --- Generic includes ---
#include "common.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/socket.h>

// --- Common component includes ---
#include "node_config.h"
#include "ota.h"
#include "uart_console.h"
#include "log_mux.h"
#include "wdt_hw.h"
#include "bme280.h" // shared with the remote node, see components/common

// --- Gateway-specific includes ---
#include "wifi.h"
#include "antenna_ctrl.h"
#include "gateway_fsm.h"
#include "lora_rx.h"
#include "led_task.h"
#include "ble_task.h"
#include "victron_ble.h"
#include "victron_crypto.h"
#include "victron_parser.h"
#include "system_metrics_task.h"

// --- Generic defines ---
#define HASH_LEN 32

// --- Function definitions ---
esp_err_t system_health_checkup(void);

#endif // MAIN_H
