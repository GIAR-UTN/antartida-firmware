/**
 * @file wdt_hw.h
 * @brief Driver contract for the *external* hardware watchdog on the new PCB.
 *
 * STUB -- owner: teammate in charge of INA + HW watchdog + antenna control.
 * This is a second, independent line of defense on top of the existing
 * software RTC watchdog (wdt_sw.h). It must be wired so that both FSMs
 * (gateway and remote) call wdt_hw_kick() from their main loop, and never
 * from inside a task that could hang waiting on network/BLE/LoRa/Campbell --
 * otherwise the external watchdog can't do its job.
 *
 * Fill in wdt_hw.c against the real GPIO once the schematic (HW-ANT-001) is
 * frozen. Until then, the stub in this component is a no-op that always
 * succeeds, so both FSMs can be developed and bring-up'd on a bare devkit
 * without it.
 */

#ifndef WDT_HW_H
#define WDT_HW_H

#include "esp_err.h"

/** One-time init of the external watchdog GPIO/peripheral. */
esp_err_t wdt_hw_init(void);

/** Must be called periodically (from the FSM's main loop) to avoid a reset. */
esp_err_t wdt_hw_kick(void);

#endif // WDT_HW_H
