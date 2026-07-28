/**
 * @file main.h
 * @brief Definition of main application functions and defines for the
 * remote acquisition node.
 */

#ifndef MAIN_H
#define MAIN_H

#include "common.h"
#include "nvs_flash.h"

#include "node_config.h"
#include "uart_console.h"
#include "log_mux.h"
#include "wdt_hw.h"
#include "bme280.h"

#include "remote_fsm.h"
#include "campbell_driver.h"
#include "lora_tx.h"

esp_err_t system_init(void);

#endif // MAIN_H
