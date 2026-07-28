/**
 * @file campbell_driver.h
 * @brief Driver contract for reading the Campbell Scientific datalogger.
 *
 * STUB -- owner: teammate in charge of the Campbell driver.
 *
 * Per the CAV 2026/2027 proposal (sec. 5.3/6.5), this needs an abstract
 * layer with two backends:
 *   - PakBus, developed and validated first against the CR1000 the IAA
 *     lent for bench testing (see github.com/LionelDarras/PyCampbellCR1000
 *     for the protocol reference Arturo Huber shared).
 *   - Legacy Campbell protocol for the real CR10X at Bahia del Diablo,
 *     which can only be validated in the field -- flagged in the proposal
 *     as the main technical risk of the whole campaign.
 *
 * remote_fsm.c only calls campbell_init()/campbell_read_latest(); which
 * backend gets compiled in is this module's concern (e.g. a Kconfig choice
 * once both backends exist), not the FSM's.
 *
 * Until a real backend exists, campbell_driver.c returns canned data so
 * remote_fsm and the LoRa/telemetry path can be developed and tested
 * without hardware.
 */

#ifndef CAMPBELL_DRIVER_H
#define CAMPBELL_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

// Placeholder fields -- replace with whatever the real CR10X/CR1000 table
// at Bahia del Diablo actually reports once that's known (Arturo Huber /
// IAA Glaciologia should have the table definition).
typedef struct {
    float air_temp_c;
    float rel_humidity_pct;
    float wind_speed_ms;
    int64_t timestamp_unix;
} campbell_record_t;

esp_err_t campbell_init(void);
esp_err_t campbell_read_latest(campbell_record_t *out);

#endif // CAMPBELL_DRIVER_H
