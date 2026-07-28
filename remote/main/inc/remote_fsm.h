/**
 * @file remote_fsm.h
 * @brief Remote acquisition node state machine (estacion meteorologica,
 * Bahia del Diablo).
 *
 * New for CAV 2026/2027 -- no equivalent existed in the CAV 2025/2026
 * firmware. Implements the asymmetric scheme from the proposal (sec. 6.5):
 * this node always initiates the exchange, the gateway just listens, so no
 * clock sync between the two is needed.
 *
 *   SLEEP (wait SIGGECA_REMOTE_WAKE_INTERVAL_SEC)
 *     -> WAKE_ACQUIRE (read the Campbell datalogger)
 *     -> LORA_TX (send one packet to the gateway)
 *     -> SLEEP
 *
 * ERROR never blocks a cycle: log and go back to sleep, same philosophy as
 * "if there's no connection, shut down anyway" in the CAV 2025/2026
 * firmware's antenna cycle.
 *
 * NOTE: SLEEP here is a plain vTaskDelay, not esp_deep_sleep_start(). Real
 * deep sleep would reset the chip and needs the whole cycle rebuilt around
 * RTC memory / wake stubs -- worth doing once WP4's energy budget (proposal
 * sec. 6.4) actually requires it, not before.
 */

#ifndef REMOTE_FSM_H
#define REMOTE_FSM_H

typedef enum {
    REMOTE_STATE_SLEEP = 0,
    REMOTE_STATE_WAKE_ACQUIRE,
    REMOTE_STATE_LORA_TX,
    REMOTE_STATE_ERROR,
} remote_state_t;

const char *remote_state_name(remote_state_t state);

void remote_fsm_task(void *pvParameter);

#endif // REMOTE_FSM_H
