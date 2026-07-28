/**
 * @file antenna_ctrl.h
 * @brief Low-level control of the Starlink antenna power relay.
 *
 * Ported from the CAV 2025/2026 firmware (gpio.c/gpio.h), but split in two:
 * this module only knows how to turn the relay on/off (the *mechanism*).
 * The *policy* -- how long to stay on, when to connect wifi, when to run
 * OTA -- now lives in gateway_fsm.c instead of being hardcoded in a
 * relay_task while(1) loop. The site-specific ON/OFF durations that used to
 * be #ifdef'd right here now live in Kconfig (NODE_RELAY_ON_MS/OFF_MS,
 * node_config.h), owned by gateway_fsm.c, not by this module.
 *
 * Owner for the next iteration: teammate in charge of INA + HW watchdog +
 * antenna control. Once the INA current sensor is wired in on the new PCB,
 * add antenna_ctrl_read_current_ma() (or a separate ina_driver.h) here so
 * gateway_fsm can log/act on antenna current draw during ACTIVE_WINDOW.
 */

#ifndef ANTENNA_CTRL_H
#define ANTENNA_CTRL_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

// --- Generic defines ---
#define ANTENNA_CTRL_GPIO_NUM       GPIO_NUM_26

// --- Function definitions ---
esp_err_t antenna_ctrl_init(void);
esp_err_t antenna_ctrl_on(void);
esp_err_t antenna_ctrl_off(void);
bool antenna_ctrl_is_on(void);

#endif // ANTENNA_CTRL_H
