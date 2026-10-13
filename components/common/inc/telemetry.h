/**
 * @file telemetry.h
 * @brief InfluxDB line-protocol telemetry client, shared by both nodes.
 *
 * Generalizes what used to be three near-identical
 * malloc+snprintf+HTTP-POST blocks in metrics_task.c (one each for Victron,
 * BME280 and system metrics) into a single internal builder
 * (see telemetry.c: send_line_v()). Adding a new measurement means adding a
 * one-function wrapper here, not copy-pasting the buffer-sizing dance again.
 *
 * NOTE: only the gateway app actually has a network path to InfluxDB. The
 * remote node's readings (Campbell datalogger) reach InfluxDB by being
 * relayed over LoRa to the gateway, which then calls these same functions --
 * see the pipeline described in the CAV 2026/2027 proposal, section 5.5.
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "esp_err.h"
#include "lora_proto.h"

esp_err_t telemetry_send_victron_mppt(int charge_state, int charger_error,
                                       float batt_v, float batt_a, float yield_wh,
                                       float solar_w, float load_a);

esp_err_t telemetry_send_bme280(float temp_c, float hum_pct, float press_hpa);

esp_err_t telemetry_send_system(void);

/**
 * @brief Gateway-only: forwards one reading relayed over LoRa by a remote
 * node (measurement "remote_weather", tags loc/role=REMOTE/node).
 *
 * @param age_s seconds between LoRa reception and now, or -1 if unknown
 *              (record survived a gateway reboot). Only informative: the
 *              point is timestamped with the Campbell clock when that one
 *              looks like a real unix time, otherwise with the server's
 *              write time (the gateway has no wall clock yet).
 */
esp_err_t telemetry_send_remote_weather(const lora_msg_telemetry_t *t,
                                        int16_t rssi_dbm, float snr_db, int32_t age_s);

#endif // TELEMETRY_H
