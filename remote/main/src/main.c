/* SiGGECA remote acquisition node firmware (estacion meteorologica, Bahia
   del Diablo).

   New for CAV 2026/2027. This node has no internet path of its own (only
   LoRa to the gateway), so its OTA images are relayed by the gateway over
   LoRa: the gateway downloads them during its Starlink window and offers
   them in the ACK to this node's hourly telemetry (see ota_lora_node.h,
   docs/LORA.md). Rollback is enabled: a new image is only confirmed once it
   reaches the gateway (remote_fsm.c).
*/

#include "main.h"

static const char *TAG = "main_app";

esp_err_t system_init(void)
{
    ESP_RETURN_ON_ERROR(app_uart_init(), TAG, "app_uart_init() failed.");
    ESP_RETURN_ON_ERROR(log_mux_init(), TAG, "Log multiplexer failed");
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(wdt_hw_init(), TAG, "wdt_hw_init() failed");
    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(bme280_init(), TAG, "BME280 was not detected on BME280_I2C_ADDR");
    ESP_RETURN_ON_ERROR(campbell_init(), TAG, "campbell_init() failed");
    ESP_RETURN_ON_ERROR(lora_tx_init(), TAG, "lora_tx_init() failed");
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "##### SiGGECA remote node #####");

    if (system_init() != ESP_OK) {
        ESP_LOGE(TAG, "system_init() failed, rebooting");
        esp_restart();
    }

    xTaskCreate(&remote_fsm_task, "remote_fsm_task", 3 * APP_MINIMAL_STACK_SIZE, NULL, CRITICAL_PRIORITY, NULL);
}
