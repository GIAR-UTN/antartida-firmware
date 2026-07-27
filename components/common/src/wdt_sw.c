/**
 * @file wdt_sw.c
 * @brief Software watchdog on top of the ESP32 RTC WDT peripheral.
 */

#include "wdt_sw.h"

static const char *TAG = "wdt_sw";
static TaskHandle_t wtd_task_xHandle = NULL;


// ----------------------- NOTE: NOT IN USE -----------------------
void wtd_task(void *pvParameter)
{
    while (1) 
    {
        rtc_wdt_feed();
        vTaskDelay(pdMS_TO_TICKS(WTD_TASK_MS));
    }
}

esp_err_t rtc_wtd_init(unsigned int timeout_ms)
{
    rtc_wdt_protect_off();
    rtc_wdt_disable();

    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, timeout_ms);

    rtc_wdt_enable();
    rtc_wdt_protect_on();


    xTaskCreate(&wtd_task, "wtd_task", 2*APP_MINIMAL_STACK_SIZE , NULL, CRITICAL_PRIORITY, &wtd_task_xHandle);
    return ESP_OK;
}
// ----------------------------------------------------------------------



/**
 * @brief Configura y enciende el rtc para una tarea critica.
 *
 * @return ESP_OK   Siempre
 */
esp_err_t rtc_wtd_config(unsigned int timeout_ms)
{
    rtc_wdt_protect_off();
    rtc_wdt_disable();

    rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
    rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
    rtc_wdt_set_time(RTC_WDT_STAGE0, timeout_ms);

    rtc_wdt_enable();
    rtc_wdt_protect_on();
    return ESP_OK;
}

/**
 * @brief Da ok al RTC y los deshabilita.
 *
 * @return ESP_OK   Siempre
 */
esp_err_t rtc_wtd_ok(void)
{
    rtc_wdt_feed();
    rtc_wdt_disable();
    return ESP_OK;
}

