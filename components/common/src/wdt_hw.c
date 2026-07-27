/**
 * @file wdt_hw.c
 * @brief STUB implementation of the external hardware watchdog driver.
 *
 * Owner: teammate in charge of INA + HW watchdog + antenna control.
 * Replace this with the real GPIO kick pattern once HW-ANT-001 is frozen.
 */
#include "wdt_hw.h"
#include "esp_log.h"

static const char *TAG = "wdt_hw_stub";

esp_err_t wdt_hw_init(void)
{
    ESP_LOGW(TAG, "wdt_hw_init(): STUB, no real hardware watchdog wired yet");
    return ESP_OK;
}

esp_err_t wdt_hw_kick(void)
{
    return ESP_OK;
}
