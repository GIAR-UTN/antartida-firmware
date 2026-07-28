set(SRCS
    "src/main.c"
    "src/remote_fsm.c"
    "src/campbell_driver.c"
    "src/lora_tx.c"
)

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "inc"
    REQUIRES
        common
        nvs_flash
        driver
        esp_driver_gpio
        esp_driver_uart
        esp_timer
)
