/**
 * @file victron_parser.h
 * @brief Parser de mensajes Victron MPPT SmartSolar
 * 
 * Parsea los datos desencriptados según el formato del protocolo Victron.
 * Replica la lógica de parse_decrypted() en solar_charger.py del repo Python.
 */

#ifndef VICTRON_PARSER_H
#define VICTRON_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Estados de carga del MPPT
 * Similar a OperationMode en base.py
 */
typedef enum {
    VICTRON_MODE_OFF = 0,
    VICTRON_MODE_LOW_POWER = 1,
    VICTRON_MODE_FAULT = 2,
    VICTRON_MODE_BULK = 3,
    VICTRON_MODE_ABSORPTION = 4,
    VICTRON_MODE_FLOAT = 5,
    VICTRON_MODE_STORAGE = 6,
    VICTRON_MODE_EQUALIZE_MANUAL = 7,
    VICTRON_MODE_INVERTING = 9,
    VICTRON_MODE_POWER_SUPPLY = 11,
    VICTRON_MODE_STARTING_UP = 245,
    VICTRON_MODE_REPEATED_ABSORPTION = 246,
    VICTRON_MODE_RECONDITION = 247,
    VICTRON_MODE_BATTERY_SAFE = 248,
    VICTRON_MODE_ACTIVE = 249,
    VICTRON_MODE_EXTERNAL_CONTROL = 252,
    VICTRON_MODE_NOT_AVAILABLE = 255
} victron_charge_state_t;

/**
 * @brief Errores del cargador
 * Similar a ChargerError en base.py
 */
typedef enum {
    VICTRON_ERROR_NO_ERROR = 0,
    VICTRON_ERROR_TEMPERATURE_BATTERY_HIGH = 1,
    VICTRON_ERROR_VOLTAGE_HIGH = 2,
    VICTRON_ERROR_REMOTE_TEMPERATURE_A = 3,
    VICTRON_ERROR_REMOTE_TEMPERATURE_B = 4,
    VICTRON_ERROR_REMOTE_TEMPERATURE_C = 5,
    VICTRON_ERROR_REMOTE_BATTERY_A = 6,
    VICTRON_ERROR_REMOTE_BATTERY_B = 7,
    VICTRON_ERROR_REMOTE_BATTERY_C = 8,
    VICTRON_ERROR_HIGH_RIPPLE = 11,
    VICTRON_ERROR_TEMPERATURE_BATTERY_LOW = 14,
    VICTRON_ERROR_TEMPERATURE_CHARGER = 17,
    VICTRON_ERROR_OVER_CURRENT = 18,
    VICTRON_ERROR_BULK_TIME = 20,
    VICTRON_ERROR_CURRENT_SENSOR = 21,
    VICTRON_ERROR_INTERNAL_TEMPERATURE_A = 22,
    VICTRON_ERROR_INTERNAL_TEMPERATURE_B = 23,
    VICTRON_ERROR_FAN = 24,
    VICTRON_ERROR_OVERHEATED = 26,
    VICTRON_ERROR_SHORT_CIRCUIT = 27,
    VICTRON_ERROR_CONVERTER_ISSUE = 28,
    VICTRON_ERROR_OVER_CHARGE = 29,
    VICTRON_ERROR_INPUT_VOLTAGE = 33,
    VICTRON_ERROR_INPUT_CURRENT = 34,
    VICTRON_ERROR_INPUT_POWER = 35,
    VICTRON_ERROR_INPUT_SHUTDOWN_VOLTAGE = 38,
    VICTRON_ERROR_INPUT_SHUTDOWN_CURRENT = 39,
    VICTRON_ERROR_INPUT_SHUTDOWN_FAILURE = 40,
    VICTRON_ERROR_UNKNOWN = 0xFF
} victron_charger_error_t;

/**
 * @brief Estructura con datos parseados del MPPT
 * Similar a SolarChargerData en solar_charger.py
 */
typedef struct {
    uint16_t model_id;                        // Model ID (Instant Readout)
    uint8_t readout_type;                     // Tipo de readout
    uint32_t iv;                              // IV del paquete
    victron_charge_state_t charge_state;      // Estado de carga
    victron_charger_error_t charger_error;    // Error del cargador
    float battery_voltage;                     // Voltaje de batería (V)
    float battery_charging_current;            // Corriente de carga (A)
    float yield_today;                         // Energía generada hoy (Wh)
    float solar_power;                         // Potencia solar actual (W)
    float external_device_load;               // Carga de dispositivo externo (A)
    bool valid;                                // true si los datos son válidos
} victron_mppt_data_t;

/**
 * @brief Parsear datos desencriptados del MPPT
 * 
 * Replica la lógica de parse_decrypted() en solar_charger.py:
 * - Lee campos bit a bit según el formato del protocolo
 * - Aplica escalas (0.01V, 0.1A, 10Wh, etc.)
 * - Maneja valores especiales (0x7FFF, 0xFFFF, 0x1FF)
 * 
 * @param decrypted Datos desencriptados
 * @param len Longitud de los datos
 * @param data Estructura donde se guardan los datos parseados
 * @return true si el parsing fue exitoso, false en caso contrario
 */
bool victron_parser_parse(const uint8_t *decrypted, size_t len, victron_mppt_data_t *data);

/**
 * @brief Imprimir datos parseados usando ESP_LOGI
 * @param data Datos a imprimir
 */
void victron_parser_print(const victron_mppt_data_t *data);

/**
 * @brief Obtener string del estado de carga
 * @param state Estado de carga
 * @return String descriptivo
 */
const char *victron_parser_charge_state_str(victron_charge_state_t state);

/**
 * @brief Obtener string del error del cargador
 * @param error Error del cargador
 * @return String descriptivo
 */
const char *victron_parser_error_str(victron_charger_error_t error);

#endif // VICTRON_PARSER_H
