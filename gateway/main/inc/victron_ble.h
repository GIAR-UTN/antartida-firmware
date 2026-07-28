/**
 * @file victron_ble.h
 * @brief Módulo BLE para conexión con MPPT Victron SmartSolar
 * 
 * Este módulo maneja:
 * - Escaneo de dispositivos BLE
 * - Conexión GATT como cliente
 * - Descubrimiento de servicios y características
 * - Suscripción a notifications
 * - Recepción de payloads cifrados
 */

#ifndef VICTRON_BLE_H
#define VICTRON_BLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "common.h"
#include "node_config.h"

// Was #ifdef MARAMBIO/ISLA_VEGA here; now a single Kconfig-backed value.
#define VICTRON_MPPT_MAC_ADDR NODE_VICTRON_MPPT_MAC

// UUIDs conocidos de Victron (según protocolo)
// Nota: Estos UUIDs pueden variar, se descubren dinámicamente
#define VICTRON_SERVICE_UUID_STR "0000fff0-0000-1000-8000-00805f9b34fb"
#define VICTRON_CHAR_UUID_STR    "0000fff1-0000-1000-8000-00805f9b34fb"

// Callback para cuando se recibe un payload cifrado
typedef void (*victron_ble_data_cb_t)(const uint8_t *data, size_t len);

/**
 * @brief Inicializar el stack BLE
 * @return ESP_OK en éxito, código de error en caso contrario
 */
esp_err_t victron_ble_init(void);

/**
 * @brief Iniciar escaneo de dispositivos BLE
 * @param mac_addr MAC address del dispositivo a buscar (NULL para escanear todos)
 * @return ESP_OK en éxito
 */
esp_err_t victron_ble_scan_start(const char *mac_addr);

/**
 * @brief Detener escaneo BLE
 * @return ESP_OK en éxito
 */
esp_err_t victron_ble_scan_stop(void);

/**
 * @brief Conectar al dispositivo MPPT
 * @param mac_addr MAC address del dispositivo
 * @return ESP_OK en éxito
 */
esp_err_t victron_ble_connect(const char *mac_addr);

/**
 * @brief Desconectar del dispositivo
 * @return ESP_OK en éxito
 */
esp_err_t victron_ble_disconnect(void);

/**
 * @brief Verificar si está conectado
 * @return true si está conectado, false en caso contrario
 */
bool victron_ble_is_connected(void);

/**
 * @brief Verificar si está escaneando
 * @return true si está escaneando
 */
bool victron_ble_is_scanning(void);

/**
 * @brief Registrar callback para recibir datos
 * @param cb Función callback
 */
void victron_ble_register_data_callback(victron_ble_data_cb_t cb);

/**
 * @brief Deinicializar BLE
 */
void victron_ble_deinit(void);

#endif // VICTRON_BLE_H
