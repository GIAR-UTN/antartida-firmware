set(SRCS
    "main.c"
    "wifi.c"
    "antenna_ctrl.c"
    "led_task.c"
    "system_metrics_task.c"
    "gateway_fsm.c"
    "lora_rx.c"
    "bme280.c"
    "ble/ble_task.c"
    "ble/victron_ble.c"
    "ble/victron_crypto.c"
    "ble/victron_parser.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "." "ble"
    REQUIRES
        common
        esp_http_client
        app_update
        esp_https_ota
        mbedtls
        nvs_flash
        esp_psram
        driver
        bt
        lwip
        esp_driver_gpio
        esp_driver_uart
        esp_driver_i2c
        esp_timer
        esp_wifi
        esp_netif
        esp_event
        protocol_examples_common
)
