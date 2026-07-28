
/**
 * @file uart_console.h
 * @brief Dedicated debug UART used by log_mux, plus the console UART0 init.
 */

#ifndef UART_CONSOLE_H
#define UART_CONSOLE_H

// --- Generic includes ---
#include "common.h"
#include "driver/uart.h"
#include "driver/gpio.h"

// --- Generic defines ---
#define APP_UART_PORT UART_NUM_2
#define APP_UART_TX_PIN GPIO_NUM_17
#define APP_UART_RX_PIN GPIO_NUM_16
#define APP_UART_BAUD   115200

// --- Function definitions ---
esp_err_t app_uart_init(void);

#endif // UART_CONSOLE_H
