/**
 * @file system_metrics_task.h
 * @brief Periodically pushes system-level metrics (heap, uptime, error
 * counter) to InfluxDB via telemetry_send_system().
 *
 * This is what's left of the CAV 2025/2026 metrics_task.c after
 * mppt_send_to_influx()/bme_send_to_influx() moved into the shared
 * telemetry component (called directly from ble_task.c / bme280.c) and
 * became telemetry_send_victron_mppt()/telemetry_send_bme280().
 */

#ifndef SYSTEM_METRICS_TASK_H
#define SYSTEM_METRICS_TASK_H

#define METRICS_INTERVAL_SEC 1

void custom_metrics_task(void *arg);

#endif // SYSTEM_METRICS_TASK_H
