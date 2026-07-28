/**
 * @file system_metrics_task.c
 * @brief See system_metrics_task.h.
 */
#include "system_metrics_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "telemetry.h"

static const char *TAG = "system_metrics_task";

void custom_metrics_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(METRICS_INTERVAL_SEC * 1000);

    while (1) {
        if (telemetry_send_system() != ESP_OK) {
            ESP_LOGE(TAG, "An error occurred while trying to send system metrics");
        }
        vTaskDelayUntil(&last, period);
    }
}
