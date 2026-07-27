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
        // While-loop hook for teammate B: poll incoming remote-node LoRa
        // packets during the active window and hand them to telemetry.
        {
            lora_rx_packet_t pkt;
            while (lora_rx_poll(&pkt)) {
                ESP_LOGI(TAG, "LoRa packet from remote node (%u bytes) -- forward to telemetry once campbell payload format is defined", (unsigned)pkt.len);
            }
        }
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
