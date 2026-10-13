/**
 * @file remote_fsm.c
 * @brief See remote_fsm.h.
 */
#include "remote_fsm.h"

#include "common.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "wdt_hw.h"
#include "campbell_driver.h"
#include "lora_tx.h"
#include "ota_lora_node.h"
#include "bme280.h"

static const char *TAG = "remote_fsm";

// BME280 is read directly here (bme280_read(), network-free) instead of via
// bme280_task: that task calls telemetry_send_bme280() straight to InfluxDB
// over HTTP, which only makes sense on the gateway (it has wifi). The remote
// node has no IP network at all -- its only way out is this LoRa frame,
// relayed by the gateway. Wire format: lora_msg_telemetry_t (lora_proto.h).
static lora_msg_telemetry_t s_msg;
static lora_tx_ack_t s_ack;

// New OTA image waiting to be confirmed by a gateway ACK.
static bool s_pending_verify;
static int s_cycles_without_ack;

const char *remote_state_name(remote_state_t state)
{
    switch (state) {
        case REMOTE_STATE_SLEEP:        return "SLEEP";
        case REMOTE_STATE_WAKE_ACQUIRE:  return "WAKE_ACQUIRE";
        case REMOTE_STATE_LORA_TX:       return "LORA_TX";
        case REMOTE_STATE_OTA_RX:        return "OTA_RX";
        case REMOTE_STATE_ERROR:         return "ERROR";
        default:                          return "UNKNOWN";
    }
}

static void check_pending_verify(void)
{
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        s_pending_verify = true;
        ESP_LOGW(TAG, "Running a new image pending verification: needs a gateway ACK within %d cycles",
                 CONFIG_SIGGECA_REMOTE_ROLLBACK_AFTER_FAILS);
    }
}

static void on_cycle_result(bool acked)
{
    if (!s_pending_verify) {
        return;
    }
    if (acked) {
        ESP_LOGI(TAG, "Gateway reachable with the new image: confirming it");
        esp_ota_mark_app_valid_cancel_rollback();
        s_pending_verify = false;
        return;
    }
    if (++s_cycles_without_ack >= CONFIG_SIGGECA_REMOTE_ROLLBACK_AFTER_FAILS) {
        ESP_LOGE(TAG, "No gateway ACK in %d cycles with the new image: rolling back", s_cycles_without_ack);
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // Only returns if there's no previous image to go back to.
        s_pending_verify = false;
    }
}

static remote_state_t next_state(remote_state_t state)
{
    switch (state) {

    case REMOTE_STATE_SLEEP:
        lora_tx_sleep();
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SIGGECA_REMOTE_WAKE_INTERVAL_SEC * 1000ULL));
        return REMOTE_STATE_WAKE_ACQUIRE;

    case REMOTE_STATE_WAKE_ACQUIRE: {
        memset(&s_msg, 0, sizeof(s_msg));
        s_msg.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
        if (s_pending_verify) {
            s_msg.flags |= LORA_TELEM_FLAG_PENDING_VERIFY;
        }

        campbell_record_t rec;
        if (campbell_read_latest(&rec) == ESP_OK) {
            s_msg.flags |= LORA_TELEM_FLAG_CAMPBELL_OK;
            s_msg.campbell_timestamp_unix = rec.timestamp_unix;
            s_msg.campbell_air_temp_c = rec.air_temp_c;
            s_msg.campbell_rel_humidity_pct = rec.rel_humidity_pct;
            s_msg.campbell_wind_speed_ms = rec.wind_speed_ms;
        } else {
            // Still transmit: the gateway learns we're alive, and an OTA
            // can still reach us if the Campbell driver itself is what broke.
            ESP_LOGE(TAG, "campbell_read_latest() failed, sending heartbeat without Campbell data");
        }

        // BME280 is best-effort too. Read into locals: the frame is packed,
        // so its fields can't be passed by pointer.
        float t, p, h;
        if (bme280_read(&t, &p, &h) == ESP_OK) {
            s_msg.flags |= LORA_TELEM_FLAG_BME280_OK;
            s_msg.bme280_temp_c = t;
            s_msg.bme280_press_hpa = p;
            s_msg.bme280_hum_pct = h;
        } else {
            ESP_LOGW(TAG, "bme280_read() failed");
        }
        return REMOTE_STATE_LORA_TX;
    }

    case REMOTE_STATE_LORA_TX:
        if (lora_tx_send_telemetry(&s_msg, &s_ack) != ESP_OK) {
            ESP_LOGE(TAG, "No ACK from gateway");
            on_cycle_result(false);
            return REMOTE_STATE_ERROR;
        }
        on_cycle_result(true);
        // Never start an OTA from an image that isn't confirmed yet.
        if (!s_pending_verify && s_ack.ota_offer && ota_lora_node_wants(s_ack.ota_version)) {
            return REMOTE_STATE_OTA_RX;
        }
        return REMOTE_STATE_SLEEP;

    case REMOTE_STATE_OTA_RX: {
        // Only returns on failure; on success it reboots into the new image.
        esp_err_t err = ota_lora_node_run(s_ack.ota_version);
        ESP_LOGW(TAG, "OTA over LoRa didn't complete (%s), retrying on a later offer", esp_err_to_name(err));
        return REMOTE_STATE_SLEEP;
    }

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
    check_pending_verify();

    // A fresh OTA image checks in right away instead of after a full sleep,
    // so a bad image is detected (and rolled back) hours sooner.
    remote_state_t state = s_pending_verify ? REMOTE_STATE_WAKE_ACQUIRE : REMOTE_STATE_SLEEP;

    while (1) {
        ESP_LOGI(TAG, "state -> %s", remote_state_name(state));
        wdt_hw_kick();
        state = next_state(state);
    }
}
