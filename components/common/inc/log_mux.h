/**
 * @file log_mux.h
 * @brief Mirrors ESP-IDF logging (ESP_LOGx) onto the debug UART and console.
 *
 * The CAV 2025/2026 firmware also had a UDP syslog path (udp_client.c) wired
 * in here but disabled/commented out. It wasn't ported in this pass since
 * it was already dead code -- reintroduce it as its own common module if a
 * campaign actually needs remote syslog again.
 */

#ifndef LOG_MUX_H
#define LOG_MUX_H

// --- Generic includes ---
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "uart_console.h"

// --- Function definitions ---
esp_err_t log_mux_init(void);

#endif // LOG_MUX_H
