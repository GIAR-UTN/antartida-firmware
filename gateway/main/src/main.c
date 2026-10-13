/* SiGGECA gateway firmware (refugio de Isla Vega / LAMBI Marambio)

   Ported from the CAV 2025/2026 firmware (esp32-ota-github). Main change:
   relay_task's ad hoc while(1) (antenna + wifi + OTA sequencing) is now
   gateway_fsm_task, see gateway_fsm.c.

   Main application file. Initialization is done here. All tasks are
   created inside this file.
*/

#include "main.h"

static const char *TAG = "main_app";

TaskHandle_t led_task_Handle = NULL;
TaskHandle_t bme280_task_Handle = NULL;
TaskHandle_t victron_ble_task_Handle = NULL;
TaskHandle_t custom_metrics_task_Handle = NULL;


static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_UNKNOWN:   return "UNKNOWN";
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL (EN pin)";
        case ESP_RST_SW:        return "SOFTWARE (esp_restart)";
        case ESP_RST_PANIC:     return "PANIC/EXCEPTION";
        case ESP_RST_INT_WDT:   return "INT WDT";
        case ESP_RST_TASK_WDT:  return "TASK WDT";
        case ESP_RST_WDT:       return "OTHER WDT";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_CPU_LOCKUP:return "CPU LOCKUP";
        case ESP_RST_PWR_GLITCH:return "POWER GLITCH";
        default:                return "OTHER";
    }
}

esp_err_t system_init(void)
{
    ESP_RETURN_ON_ERROR(app_uart_init(), TAG, "app_uart_init() failed.");
    ESP_RETURN_ON_ERROR(log_mux_init(), TAG, "Log multiplexer failed");
    ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS init failed");
    get_sha256_of_partitions();
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "esp_event_loop_create_default() failed.");
    ESP_RETURN_ON_ERROR(antenna_ctrl_init(), TAG, "antenna_ctrl_init() failed");
    ESP_RETURN_ON_ERROR(wdt_hw_init(), TAG, "wdt_hw_init() failed");
    // Not fatal on purpose: a missing/broken RFM95 must not take down
    // Starlink + Victron (or trigger a rollback of an otherwise good image),
    // which are this node's primary job. The remote node keeps retrying.
    if (lora_rx_init() != ESP_OK) {
        ESP_LOGE(TAG, "lora_rx_init() failed: remote node link disabled until next boot");
    }
    ESP_RETURN_ON_ERROR(i2c_master_init(), TAG, "I2C init failed");
    ESP_RETURN_ON_ERROR(bme280_init(), TAG, "BME280 was not detected on BME280_I2C_ADDR");
    ESP_RETURN_ON_ERROR(led_task_init(), TAG, "led_task_init() failed");
    ESP_RETURN_ON_ERROR(wifi_init(), TAG, "WiFi init failed");

    return ESP_OK;
}

esp_err_t system_health_checkup(void)
{
    /**
     * @brief Runs a health check and then validates the operation of a new ota image.
     *
     * If the previous reset was abnormal (panic, watchdog, brownout, glitch,
     * lockup), run OTA once before bringing up the rest of the system, to
     * maximize the chance that a fix already sitting on the server resolves
     * the issue.
     */
    esp_reset_reason_t r = esp_reset_reason();

    if ((r != ESP_RST_SW) && (r != ESP_RST_POWERON)) {
        ESP_LOGW(TAG, "Reset reason: %s (%d)", reset_reason_str(r), (int)r);

        ESP_RETURN_ON_ERROR(nvs_flash_init(), TAG, "NVS init failed");
        get_sha256_of_partitions();
        ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "esp_event_loop_create_default() failed.");
        ESP_RETURN_ON_ERROR(antenna_ctrl_init(), TAG, "antenna_ctrl_init() failed");
        ESP_RETURN_ON_ERROR(wifi_init(), TAG, "WiFi init failed");
        ESP_LOGI(TAG, "Running OTA..");
        if (wifi_connect_if_needed() == ESP_FAIL) {
            ESP_LOGE(TAG, "wifi_connect_if_needed() error on abnormal-reset OTA check");
            esp_restart();
        }
        execute_ota();
        // Will reboot after OTA
    }

    return system_init();
}

void app_main(void)
{
    ESP_LOGI(TAG, "##### SiGGECA gateway #####");

    if (system_health_checkup() == ESP_OK) {
        ESP_LOGI(TAG, "System health check passed.");
        if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
            ESP_LOGE(TAG, "Error occurred during esp_ota_mark_app_valid_cancel_rollback(). Rebooting..");
            esp_restart();
        }
    } else {
        ESP_LOGE(TAG, "Error occurred during system health check.");
        ESP_LOGE(TAG, "Running Rollback.");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        esp_restart(); // Fallback if there's no valid image in the other slot.
    }

    xTaskCreate(&led_task, "led_task", 2 * APP_MINIMAL_STACK_SIZE, NULL, LOW_PRIORITY, &led_task_Handle);
    xTaskCreate(&bme280_task, "bme280_task", 2 * APP_MINIMAL_STACK_SIZE, NULL, MED_PRIORITY, &bme280_task_Handle);
    xTaskCreate(&victron_ble_task, "victron_ble_task", 6 * APP_MINIMAL_STACK_SIZE, NULL, MED_PRIORITY, &victron_ble_task_Handle);
    xTaskCreate(&custom_metrics_task, "metrics_task", 2 * APP_MINIMAL_STACK_SIZE, NULL, MED_PRIORITY, &custom_metrics_task_Handle);

    xTaskCreate(&gateway_fsm_task, "gateway_fsm_task", 2 * APP_MINIMAL_STACK_SIZE, NULL, CRITICAL_PRIORITY, NULL);
}
