#include "uart_console.h"


esp_err_t app_uart_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate = APP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(APP_UART_PORT, &uart_cfg);
    uart_set_pin(APP_UART_PORT, APP_UART_TX_PIN, APP_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(APP_UART_PORT, 1024, 0, 0, NULL, 0);

    uart_param_config(UART_NUM_0, &uart_cfg);
    uart_set_pin(UART_NUM_0, GPIO_NUM_1, GPIO_NUM_3,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    return ESP_OK;
}
// ------------------------------------------------