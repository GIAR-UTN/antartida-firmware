/**
 * @file remote_fsm.c
 * @brief See remote_fsm.h.
 */
#include "remote_fsm.h"

#include "common.h"
#include "wdt_hw.h"
#include "campbell_driver.h"
#include "lora_tx.h"
#include "bme280.h"

static const char *TAG = "remote_fsm";

// Placeholder wire format: raw struct bytes over LoRa. Replace with a real,
// versioned packet format (with a node id) once the LoRa driver and the
// gateway's telemetry relay (proposal sec. 5.5) are wired up for real.
//
// BME280 is read directly here (bme280_read(), network-free) instead of
// via bme280_task: that task calls telemetry_send_bme280() straight to
// InfluxDB over HTTP, which only makes sense on the gateway (it has wifi).
// The remote node has no IP network at all -- its only way out is this
// LoRa packet, relayed by the gateway.
typedef struct {
    campbell_record_t campbell;
    float bme280_temp_c;
    float bme280_press_hpa;
    float bme280_hum_pct;
} remote_payload_t;

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
    static remote_payload_t payload;

    switch (state) {

    case REMOTE_STATE_SLEEP:
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SIGGECA_REMOTE_WAKE_INTERVAL_SEC * 1000));
        return REMOTE_STATE_WAKE_ACQUIRE;

    case REMOTE_STATE_WAKE_ACQUIRE:
        if (campbell_read_latest(&payload.campbell) != ESP_OK) {
            ESP_LOGE(TAG, "campbell_read_latest() failed");
            return REMOTE_STATE_ERROR;
        }
        // BME280 is best-effort: don't drop the Campbell reading just
        // because the local sensor hiccuped.
        if (bme280_read(&payload.bme280_temp_c, &payload.bme280_press_hpa,
                         &payload.bme280_hum_pct) != ESP_OK) {
            ESP_LOGW(TAG, "bme280_read() failed, sending Campbell data only");
        }
        return REMOTE_STATE_LORA_TX;

    case REMOTE_STATE_LORA_TX:
        if (lora_tx_send((const uint8_t *)&payload, sizeof(payload)) != ESP_OK) {
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
