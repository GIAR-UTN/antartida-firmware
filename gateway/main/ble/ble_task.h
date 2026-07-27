/**
 * @file ble_task.h
 * @brief Tarea FreeRTOS para lectura BLE Victron.
 */

#ifndef BLE_TASK_H
#define BLE_TASK_H

#include <stdint.h>

// Intervalo por defecto para procesar lecturas BLE (ms)
#define VICTRON_BLE_READ_INTERVAL_MS_DEFAULT 10000

/**
 * @brief Inicia la tarea BLE (scan → connect → notify → decrypt → parse).
 */
void victron_ble_task_start(void);

void victron_ble_task(void *param);

#endif // BLE_TASK_H
