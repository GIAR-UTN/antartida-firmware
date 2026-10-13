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
 *     -> WAKE_ACQUIRE (read the Campbell datalogger + BME280)
 *     -> LORA_TX (send one TELEMETRY frame, wait for the gateway's ACK)
 *     -> OTA_RX (only if the ACK offered a version we want)
 *     -> SLEEP
 *
 * A Campbell read failure no longer skips the cycle: the frame goes out
 * with LORA_TELEM_FLAG_CAMPBELL_OK cleared, so the gateway still sees the
 * node alive and -- more importantly -- can still deliver an OTA that fixes
 * a broken Campbell driver.
 *
 * ERROR never blocks a cycle: log and go back to sleep, same philosophy as
 * "if there's no connection, shut down anyway" in the CAV 2025/2026
 * firmware's antenna cycle.
 *
 * New OTA image (ESP_OTA_IMG_PENDING_VERIFY): the first cycle runs right
 * away instead of sleeping first, the image is confirmed on the first
 * gateway ACK, and it rolls back after
 * CONFIG_SIGGECA_REMOTE_ROLLBACK_AFTER_FAILS cycles without one.
 *
 * NOTE: SLEEP here is a plain vTaskDelay, not esp_deep_sleep_start(). Real
 * deep sleep would reset the chip and needs the whole cycle rebuilt around
 * RTC memory / wake stubs -- worth doing once WP4's energy budget (proposal
 * sec. 6.4) actually requires it, not before. The radio itself does go to
 * SLEEP between cycles.
 */

#ifndef REMOTE_FSM_H
#define REMOTE_FSM_H

typedef enum {
    REMOTE_STATE_SLEEP = 0,
    REMOTE_STATE_WAKE_ACQUIRE,
    REMOTE_STATE_LORA_TX,
    REMOTE_STATE_OTA_RX,
    REMOTE_STATE_ERROR,
} remote_state_t;

const char *remote_state_name(remote_state_t state);

void remote_fsm_task(void *pvParameter);

#endif // REMOTE_FSM_H
