/**
 * @file ota.c
 * @brief Over-The-Air update via esp_https_ota, downloading from NODE_OTA_URL.
 *
 * Ported from the CAV 2025/2026 firmware (ota.c). Dropped
 * simple_ota_example_task(), which was leftover ESP-IDF example boilerplate
 * never called from anywhere in that firmware -- execute_ota() is the only
 * OTA entry point actually used (from the gateway FSM).
 */
#include "ota.h"

static const char *TAG = "ota";

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

void execute_ota(void)
{
    ESP_LOGI(TAG, "Starting OTA task");
    esp_http_client_config_t config = {
        .url = NODE_OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);

    uint8_t counter = 0;
    esp_err_t ret = ESP_FAIL;
    while ((counter < 10) && (ret != ESP_OK)) {
        rtc_wtd_config(120 * 1000); // 2 minutos de tiempo para esp_https_ota()
        ret = esp_https_ota(&ota_config);
        rtc_wtd_ok();

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "Firmware upgrade failed. Retrying...");
            vTaskDelay(pdMS_TO_TICKS(OTA_RETRY_TIMEOUT_MS));
        }
        counter++;
    }
    ESP_LOGE(TAG, "Firmware upgrade failed. Max retry count exceeded...");
    esp_restart();
}
