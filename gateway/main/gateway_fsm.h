/**
 * @file gateway_fsm.h
 * @brief Gateway node state machine (refugio de Isla Vega / LAMBI Marambio).
 *
 * Replaces the ad hoc while(1) loop in the CAV 2025/2026 firmware's
 * relay_task (gpio.c), which mixed relay control, wifi connect/disconnect
 * and OTA sequencing directly in one function with hardcoded timings.
 *
 * Behavior preserved 1:1 from that loop:
 *   SLEEP (antenna off, NODE_RELAY_OFF_MS)
 *     -> ANTENNA_ON_WIFI_CONNECT (relay on, connect wifi)
 *     -> ACTIVE_WINDOW (hold the link for NODE_RELAY_ON_MS)
 *     -> ANTENNA_OFF (disconnect wifi, relay off, NODE_RELAY_OFF_MS)
 *     -> OTA_WARMUP (relay on again, ~120s for Starlink to come up)
 *     -> OTA_CHECK (stop the other tasks, reconnect wifi, run OTA)
 *
 * execute_ota() always ends in esp_restart() (success or exhausted
 * retries), so this FSM does not loop back to SLEEP itself -- the next
 * cycle begins on the following boot. ERROR is reached only if wifi
 * connect fails outright, and also ends in a reboot.
 */

#ifndef GATEWAY_FSM_H
#define GATEWAY_FSM_H

typedef enum {
    GATEWAY_STATE_SLEEP = 0,
    GATEWAY_STATE_ANTENNA_ON_WIFI_CONNECT,
    GATEWAY_STATE_ACTIVE_WINDOW,
    GATEWAY_STATE_ANTENNA_OFF,
    GATEWAY_STATE_OTA_WARMUP,
    GATEWAY_STATE_OTA_CHECK,
    GATEWAY_STATE_ERROR,
} gateway_state_t;

const char *gateway_state_name(gateway_state_t state);

/** FreeRTOS task entry point. Critical priority, same as the old relay_task. */
void gateway_fsm_task(void *pvParameter);

#endif // GATEWAY_FSM_H
