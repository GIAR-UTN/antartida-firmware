/**
 * @file led_task.c
 * @brief Control del LED onboard con parpadeo configurable.
 */

#include "led_task.h"

static const char *TAG = "led_task.c";

void led_task(void *pvParameter)
{

    ESP_LOGI(TAG, "Parpadeo %lu ms ON / %lu ms OFF", LED_ON_MS, LED_OFF_MS);
    while (1) 
    {
        gpio_set_level(LED_GPIO_NUM, LED_ACTIVE_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(LED_ON_MS));

        gpio_set_level(LED_GPIO_NUM, !LED_ACTIVE_LEVEL);
        vTaskDelay(pdMS_TO_TICKS(LED_OFF_MS));
    }
}

esp_err_t led_task_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << LED_GPIO_NUM,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio_config() failed.");
    ESP_RETURN_ON_ERROR(gpio_set_level(LED_GPIO_NUM, !LED_ACTIVE_LEVEL), TAG, "gpio_set_level() failed.");
    
    return ESP_OK;
}
