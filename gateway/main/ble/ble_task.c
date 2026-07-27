/**
 * @file ble_task.c
 * @brief Tarea BLE para Victron MPPT usando NimBLE.
 */

#include "ble_task.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "victron_ble.h"
#include "victron_crypto.h"
#include "victron_parser.h"
#include "telemetry.h"

static const char *TAG = "BLE_TASK";

static victron_crypto_ctx_t s_crypto_ctx;
static uint32_t s_read_interval_ms = VICTRON_BLE_READ_INTERVAL_MS_DEFAULT;
static int64_t s_last_process_us = 0;
static int64_t s_last_rx_us = 0;
static int64_t s_last_status_log_us = 0;

static void victron_on_ble_data(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        ESP_LOGW(TAG, "Payload BLE vacío o nulo");
        return;
    }

    int64_t now_us = esp_timer_get_time();
    s_last_rx_us = now_us;
    if (s_last_process_us != 0 &&
        (now_us - s_last_process_us) < ((int64_t)s_read_interval_ms * 1000)) {
        return;
    }
    s_last_process_us = now_us;

    // ESP_LOGI(TAG, "Notificación BLE recibida, len=%d", (int)len);

    uint8_t decrypted[256];
    size_t decrypted_len = sizeof(decrypted);

    esp_err_t err = victron_crypto_decrypt(&s_crypto_ctx, data, len, decrypted, &decrypted_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error while decrypting payload: 0x%x", err);
        return;
    }

    // ESP_LOGI(TAG, "Payload desencriptado (%d bytes)", (int)decrypted_len);

    victron_mppt_data_t mppt_data;
    if (!victron_parser_parse(decrypted, decrypted_len, &mppt_data)) {
        ESP_LOGE(TAG, "Payload could not be parsed");
        return;
    }

    // TODO: Remove if not used
    // victron_crypto_get_last_header(&s_crypto_ctx, &mppt_data.model_id, &mppt_data.readout_type, &mppt_data.iv);
    // victron_parser_print(&mppt_data);
    
    telemetry_send_victron_mppt((int)mppt_data.charge_state, (int)mppt_data.charger_error,
                        mppt_data.battery_voltage, mppt_data.battery_charging_current,
                        mppt_data.yield_today, mppt_data.solar_power, mppt_data.external_device_load);
}

void victron_ble_task(void *param)
{
    ESP_LOGI(TAG, "Iniciando BLE task");


    esp_err_t err = victron_crypto_init(&s_crypto_ctx, VICTRON_DEVICE_KEY_STR);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Crypto init falló: 0x%x", err);
        vTaskDelete(NULL);
        return;
    }

    err = victron_ble_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE init falló: 0x%x", err);
        vTaskDelete(NULL);
        return;
    }

    victron_ble_register_data_callback(victron_on_ble_data);

    vTaskDelay(pdMS_TO_TICKS(1500));

    int64_t last_connect_attempt_us = 0;
    const int64_t retry_interval_us = 5000LL * 1000LL;

    while (1) {
        if (!victron_ble_is_scanning()) {
            int64_t now_us = esp_timer_get_time();
            if ((now_us - last_connect_attempt_us) >= retry_interval_us) {
                last_connect_attempt_us = now_us;
                ESP_LOGI(TAG, "Iniciando escaneo BLE (MPPT %s)", VICTRON_MPPT_MAC_ADDR);
                victron_ble_scan_start(VICTRON_MPPT_MAC_ADDR);
            }
        }

        // int64_t now_us = esp_timer_get_time();
        // if ((now_us - s_last_status_log_us) >= 10000000LL) {
        //     s_last_status_log_us = now_us;
        //     if (s_last_rx_us == 0) {
        //         ESP_LOGW(TAG, "BLE activo: sin datos recibidos todavía");
        //     } else if ((now_us - s_last_rx_us) > (int64_t)s_read_interval_ms * 3000) {
        //         ESP_LOGW(TAG, "BLE activo: sin datos en los últimos %d s",
        //                  (int)((now_us - s_last_rx_us) / 1000000LL));
        //     }
        // }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
