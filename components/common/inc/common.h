/**
 * @file common.h
 * @brief Generic definitions shared by the gateway and remote firmware apps.
 *
 * Site/node-specific values (wifi, timings, keys, telemetry endpoint) do NOT
 * live here anymore as #ifdef blocks -- see node_config.h, which is backed
 * by Kconfig so a single `idf.py menuconfig` (or sdkconfig.defaults per
 * site) is the only source of truth.
 */

#ifndef COMMON_H
#define COMMON_H

// --- Generic includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "string.h"

// --- Generic defines ---
#define LOW_PRIORITY 1
#define MED_PRIORITY 2
#define HIGH_PRIORITY 3
#define CRITICAL_PRIORITY 4
#define APP_MINIMAL_STACK_SIZE 2048

#endif // COMMON_H
