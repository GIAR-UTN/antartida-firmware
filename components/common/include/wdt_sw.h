/**
 * @file wdt_sw.h
 * @brief Software watchdog built on the ESP32's internal RTC WDT peripheral.
 *
 * This is distinct from the *external hardware* watchdog on the new PCB
 * (see wdt_hw.h, gateway-side driver) -- this one already existed in the
 * CAV 2025/2026 firmware (as wtd.c) and is kept as a second, independent
 * line of defense.
 */

#ifndef WDT_SW_H
#define WDT_SW_H

// --- Generic includes ---
#include "common.h"
#include "rtc_wdt.h"

// --- Generic defines ---
#define WTD_TIMER_MS 10000
#define WTD_TASK_MS 9000

// --- Function definitions ---
void wtd_task(void *pvParameter);
esp_err_t rtc_wtd_init(unsigned int);
esp_err_t rtc_wtd_config(unsigned int timeout_ms);
esp_err_t rtc_wtd_ok(void);

#endif // WDT_SW_H
