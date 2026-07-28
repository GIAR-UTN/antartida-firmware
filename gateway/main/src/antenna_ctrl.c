/**
 * @file antenna_ctrl.c
 * @brief Low-level relay control for the Starlink antenna.
 *
 * DISCREPANCY FOUND while porting from gpio.c (CAV 2025/2026): gpio_init()
 * set the initial GPIO level to 1 with a comment saying "Starlink OFF", but
 * relay_task()'s very first action was gpio_set_level(RELAY_GPIO_NUM, 1)
 * logged as "Relay ON" -- i.e. level 1 meant ON everywhere the relay was
 * actually driven. Either the antenna was briefly energized at boot before
 * the old relay_task re-asserted it (harmless, since it set the same level
 * again), or the boot comment was simply stale. This port standardizes on
 * the convention the task loop actually used (1 = ON, 0 = OFF) and starts
 * OFF explicitly via antenna_ctrl_off(). Verify against the real relay
 * wiring on HW-ANT-001 before trusting this blindly.
 */
#include "antenna_ctrl.h"
#include "common.h"

static const char *TAG = "antenna_ctrl";
static bool s_is_on = false;

esp_err_t antenna_ctrl_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << ANTENNA_CTRL_GPIO_NUM,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    return antenna_ctrl_off();
}

esp_err_t antenna_ctrl_on(void)
{
    esp_err_t err = gpio_set_level(ANTENNA_CTRL_GPIO_NUM, 1);
    if (err == ESP_OK) {
        s_is_on = true;
        ESP_LOGI(TAG, "Antenna relay ON");
    }
    return err;
}

esp_err_t antenna_ctrl_off(void)
{
    esp_err_t err = gpio_set_level(ANTENNA_CTRL_GPIO_NUM, 0);
    if (err == ESP_OK) {
        s_is_on = false;
        ESP_LOGI(TAG, "Antenna relay OFF");
    }
    return err;
}

bool antenna_ctrl_is_on(void)
{
    return s_is_on;
}
