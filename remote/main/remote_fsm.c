/**
 * @file remote_fsm.c
 * @brief See remote_fsm.h.
 */
#include "remote_fsm.h"

#include "common.h"
#include "wdt_hw.h"
#include "campbell_driver.h"
#include "lora_tx.h"

static const char *TAG = "remote_fsm";

const char *remote_state_name(remote_state_t state)
{
    switch (state) {
        case REMOTE_STATE_SLEEP:        return "SLEEP";
        case REMOTE_STATE_WAKE_ACQUIRE:  return "WAKE_ACQUIRE";
        case REMOTE_STATE_LORA_TX:       return "LORA_TX";
        case REMOTE_STATE_ERROR:         return "ERROR";
        default:                          return "UNKNOWN";
    }
}

static remote_state_t next_state(remote_state_t state)
{
    static campbell_record_t last_record;

    switch (state) {

    case REMOTE_STATE_SLEEP:
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SIGGECA_REMOTE_WAKE_INTERVAL_SEC * 1000));
        return REMOTE_STATE_WAKE_ACQUIRE;

    case REMOTE_STATE_WAKE_ACQUIRE:
        if (campbell_read_latest(&last_record) != ESP_OK) {
            ESP_LOGE(TAG, "campbell_read_latest() failed");
            return REMOTE_STATE_ERROR;
        }
        return REMOTE_STATE_LORA_TX;

    case REMOTE_STATE_LORA_TX:
        // Placeholder wire format: raw struct bytes. Replace with a real
        // packet format (versioned, with a node id) once the LoRa driver
        // and the gateway's telemetry relay (proposal sec. 5.5) are wired
        // up for real.
        if (lora_tx_send((const uint8_t *)&last_record, sizeof(last_record)) != ESP_OK) {
            ESP_LOGE(TAG, "lora_tx_send() failed");
            return REMOTE_STATE_ERROR;
        }
        return REMOTE_STATE_SLEEP;

    case REMOTE_STATE_ERROR:
    default:
        // Never block a cycle on an error -- log it and go back to sleep,
        // same philosophy as the gateway's antenna cycle in CAV 2025/2026.
        ESP_LOGE(TAG, "Cycle failed, will retry next wake");
        return REMOTE_STATE_SLEEP;
    }
}

void remote_fsm_task(void *pvParameter)
{
    (void)pvParameter;
    remote_state_t state = REMOTE_STATE_SLEEP;

    while (1) {
        ESP_LOGI(TAG, "state -> %s", remote_state_name(state));
        wdt_hw_kick();
        state = next_state(state);
    }
}
