/**
 * @file gateway_fsm.c
 * @brief See gateway_fsm.h.
 */
#include "gateway_fsm.h"

#include "common.h"
#include "node_config.h"
#include "antenna_ctrl.h"
#include "wifi.h"
#include "ota.h"
#include "wdt_hw.h"
#include "lora_rx.h"
#include "ota_lora_gw.h"
#include "remote_image.h"

static const char *TAG = "gateway_fsm";

extern TaskHandle_t led_task_Handle;
extern TaskHandle_t bme280_task_Handle;
extern TaskHandle_t victron_ble_task_Handle;
extern TaskHandle_t custom_metrics_task_Handle;

const char *gateway_state_name(gateway_state_t state)
{
    switch (state) {
        case GATEWAY_STATE_SLEEP:                    return "SLEEP";
        case GATEWAY_STATE_ANTENNA_ON_WIFI_CONNECT:   return "ANTENNA_ON_WIFI_CONNECT";
        case GATEWAY_STATE_ACTIVE_WINDOW:             return "ACTIVE_WINDOW";
        case GATEWAY_STATE_ANTENNA_OFF:               return "ANTENNA_OFF";
        case GATEWAY_STATE_OTA_WARMUP:                return "OTA_WARMUP";
        case GATEWAY_STATE_OTA_CHECK:                 return "OTA_CHECK";
        case GATEWAY_STATE_ERROR:                     return "ERROR";
        default:                                       return "UNKNOWN";
    }
}

static void wait_for_lora_ota_session(void)
{
    const TickType_t max_wait = pdMS_TO_TICKS(CONFIG_SIGGECA_LORA_OTA_MAX_DEFER_MIN * 60ULL * 1000);
    const TickType_t t0 = xTaskGetTickCount();
    if (!ota_lora_gw_busy()) {
        return;
    }
    ESP_LOGI(TAG, "LoRa OTA session to the remote node in progress, postponing gateway OTA");
    while (ota_lora_gw_busy() && (xTaskGetTickCount() - t0) < max_wait) {
        wdt_hw_kick();
        vTaskDelay(pdMS_TO_TICKS(10 * 1000));
    }
    if (ota_lora_gw_busy()) {
        ESP_LOGW(TAG, "LoRa OTA session still running after %d min, continuing anyway",
                 CONFIG_SIGGECA_LORA_OTA_MAX_DEFER_MIN);
    }
}

static gateway_state_t next_state(gateway_state_t state)
{
    switch (state) {

    case GATEWAY_STATE_SLEEP:
        antenna_ctrl_on();
        if (wifi_connect_if_needed() == ESP_FAIL) {
            ESP_LOGE(TAG, "wifi_connect_if_needed() failed");
            return GATEWAY_STATE_ERROR;
        }
        return GATEWAY_STATE_ANTENNA_ON_WIFI_CONNECT;

    case GATEWAY_STATE_ANTENNA_ON_WIFI_CONNECT:
        // Remote-node readings received over LoRa (the lora_rx task listens
        // and ACKs all the time; here we only forward what it buffered).
        lora_rx_flush_to_telemetry();
        vTaskDelay(pdMS_TO_TICKS(NODE_RELAY_ON_MS));
        return GATEWAY_STATE_ACTIVE_WINDOW;

    case GATEWAY_STATE_ACTIVE_WINDOW:
        if (wifi_disconnect_if_connected() == ESP_FAIL) {
            ESP_LOGE(TAG, "wifi_disconnect_if_connected() failed");
        }
        antenna_ctrl_off();
        vTaskDelay(pdMS_TO_TICKS(NODE_RELAY_OFF_MS));
        return GATEWAY_STATE_ANTENNA_OFF;

    case GATEWAY_STATE_ANTENNA_OFF:
        // OTA_CHECK always ends in esp_restart(), which would cut a LoRa OTA
        // session to the remote node halfway (they take tens of minutes).
        // First stop new sessions from starting (offers resume after the
        // reboot), then let a running one finish, with an upper bound.
        ota_lora_gw_hold_offers(true);
        wait_for_lora_ota_session();
        antenna_ctrl_on();
        vTaskDelay(pdMS_TO_TICKS(120 * 1000)); // let Starlink initialize
        return GATEWAY_STATE_OTA_WARMUP;

    case GATEWAY_STATE_OTA_WARMUP:
        // Stop the other tasks to avoid conflicts while OTA runs.
        vTaskDelete(led_task_Handle);
        vTaskDelete(bme280_task_Handle);
        vTaskDelete(victron_ble_task_Handle);
        vTaskDelete(custom_metrics_task_Handle);
        vTaskDelay(pdMS_TO_TICKS(10));

        ESP_LOGI(TAG, "Running OTA..");
        if (wifi_connect_if_needed() == ESP_FAIL) {
            ESP_LOGE(TAG, "wifi_connect_if_needed() failed before OTA");
            return GATEWAY_STATE_ERROR;
        }
        // Readings received during the OFF window: send them now, the reboot
        // after OTA would otherwise make them wait (in RTC memory) a whole cycle.
        lora_rx_flush_to_telemetry();
        // Refresh the remote node's image before our own OTA reboots us.
        {
            esp_err_t err = remote_image_fetch();
            if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
                ESP_LOGW(TAG, "remote_image_fetch() failed (%s)", esp_err_to_name(err));
            }
        }
        return GATEWAY_STATE_OTA_CHECK;

    case GATEWAY_STATE_OTA_CHECK:
        execute_ota(); // always ends in esp_restart(), success or not
        return GATEWAY_STATE_ERROR; // unreachable

    case GATEWAY_STATE_ERROR:
    default:
        ESP_LOGE(TAG, "Unrecoverable FSM error, rebooting");
        esp_restart();
        return GATEWAY_STATE_ERROR; // unreachable
    }
}

void gateway_fsm_task(void *pvParameter)
{
    (void)pvParameter;
    gateway_state_t state = GATEWAY_STATE_SLEEP;

    while (1) {
        ESP_LOGI(TAG, "state -> %s", gateway_state_name(state));
        wdt_hw_kick();
        state = next_state(state);
    }
}
