set(SRCS
    "main.c"
    "remote_fsm.c"
    "campbell_driver.c"
    "lora_tx.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "."
    REQUIRES
        common
        nvs_flash
        driver
        esp_driver_gpio
        esp_driver_uart
        esp_timer
)
