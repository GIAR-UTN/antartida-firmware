/**
 * @file led_task.h
 * @brief Tarea FreeRTOS para controlar LED onboard.
 */

#ifndef LED_TASK_H
#define LED_TASK_H


// --- Generic includes ---
#include "common.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

// --- Project related includes ---



// --- Generic defines ---
#define LED_GPIO_NUM GPIO_NUM_2
#define LED_ACTIVE_LEVEL 1
// #define LED_ON_MS  (200)  // 200ms segundos ON
// #define LED_OFF_MS (200)  // 200ms segundos OFF
#define LED_ON_MS  (500)  // 500ms segundos ON
#define LED_OFF_MS (500)  // 500ms segundos OFF


// --- Function definitions ---
esp_err_t led_task_init(void);
void led_task(void *pvParameter);



#endif // LED_TASK_H
