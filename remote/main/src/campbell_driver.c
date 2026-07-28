/**
 * @file campbell_driver.c
 * @brief STUB. Owner: teammate in charge of the Campbell driver.
 */
#include "campbell_driver.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "campbell_driver_stub";

esp_err_t campbell_init(void)
{
    ESP_LOGW(TAG, "campbell_init(): STUB, no CS I/O / PakBus wired yet");
    return ESP_OK;
}

esp_err_t campbell_read_latest(campbell_record_t *out)
{
    out->air_temp_c = -10.0f;
    out->rel_humidity_pct = 50.0f;
    out->wind_speed_ms = 5.0f;
    out->timestamp_unix = esp_timer_get_time() / 1000000;
    ESP_LOGW(TAG, "campbell_read_latest(): STUB, returning canned data");
    return ESP_OK;
}
