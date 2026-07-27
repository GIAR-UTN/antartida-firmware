/**
 * @file victron_parser.c
 * @brief Parser de mensajes Victron MPPT SmartSolar.
 *
 * Replica la lógica de solar_charger.py en victron-ble:
 * - Lectura bit a bit LSB → MSB
 * - Aplicación de escalas y valores especiales
 */

#include "victron_parser.h"

#include <string.h>
#include <math.h>

#include "esp_log.h"

static const char *TAG_PARSER = "VICTRON_PARSER";

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t index;
    bool msb_first;
} bit_reader_t;

static bool bit_reader_read_bit(bit_reader_t *br, uint8_t *out)
{
    size_t byte_index = br->index >> 3;
    if (byte_index >= br->len) {
        return false;
    }
    uint8_t shift = br->msb_first ? (7 - (br->index & 7)) : (br->index & 7);
    uint8_t bit = (br->data[byte_index] >> shift) & 0x01;
    br->index++;
    *out = bit;
    return true;
}

static bool bit_reader_read_unsigned(bit_reader_t *br, int num_bits, uint32_t *out)
{
    uint32_t value = 0;
    for (int i = 0; i < num_bits; i++) {
        uint8_t bit = 0;
        if (!bit_reader_read_bit(br, &bit)) {
            return false;
        }
        value |= ((uint32_t)bit << i);
    }
    *out = value;
    return true;
}

static int32_t to_signed_int(uint32_t value, int num_bits)
{
    uint32_t sign_bit = 1U << (num_bits - 1);
    if (value & sign_bit) {
        return (int32_t)(value - (1U << num_bits));
    }
    return (int32_t)value;
}

static bool bit_reader_read_signed(bit_reader_t *br, int num_bits, int32_t *out)
{
    uint32_t value = 0;
    if (!bit_reader_read_unsigned(br, num_bits, &value)) {
        return false;
    }
    *out = to_signed_int(value, num_bits);
    return true;
}

static void log_float(const char *label, float value, const char *unit)
{
    if (isnan(value)) {
        ESP_LOGI(TAG_PARSER, "%s: N/A", label);
    } else {
        ESP_LOGI(TAG_PARSER, "%s: %.2f %s", label, value, unit);
    }
}

static bool victron_parser_parse_with_mode(const uint8_t *decrypted, size_t len,
                                           victron_mppt_data_t *data, bool msb_first)
{
    if (!decrypted || !data) {
        return false;
    }

    memset(data, 0, sizeof(*data));
    data->valid = false;

    const int required_bits = 8 + 8 + 16 + 16 + 16 + 16 + 9;
    if ((int)(len * 8) < required_bits) {
        ESP_LOGE(TAG_PARSER, "Payload desencriptado demasiado corto: %d bytes",
                 (int)len);
        return false;
    }

    bit_reader_t reader = {
        .data = decrypted,
        .len = len,
        .index = 0,
        .msb_first = msb_first,
    };

    uint32_t charge_state = 0;
    uint32_t charger_error = 0;
    int32_t battery_voltage = 0;
    int32_t battery_charging_current = 0;
    uint32_t yield_today = 0;
    uint32_t solar_power = 0;
    uint32_t external_device_load = 0;

    if (!bit_reader_read_unsigned(&reader, 8, &charge_state) ||
        !bit_reader_read_unsigned(&reader, 8, &charger_error) ||
        !bit_reader_read_signed(&reader, 16, &battery_voltage) ||
        !bit_reader_read_signed(&reader, 16, &battery_charging_current) ||
        !bit_reader_read_unsigned(&reader, 16, &yield_today) ||
        !bit_reader_read_unsigned(&reader, 16, &solar_power) ||
        !bit_reader_read_unsigned(&reader, 9, &external_device_load)) {
        ESP_LOGE(TAG_PARSER, "Error leyendo bits del payload");
        return false;
    }

    data->charge_state = (charge_state == 0xFF)
                             ? VICTRON_MODE_NOT_AVAILABLE
                             : (victron_charge_state_t)charge_state;
    data->charger_error = (charger_error == 0xFF)
                              ? VICTRON_ERROR_UNKNOWN
                              : (victron_charger_error_t)charger_error;

    // ESP_LOGI(TAG_PARSER, "RAW state=%u error=%u batt_v=%d batt_a=%d yield=%u solar=%u ext=%u",
    //          (unsigned)charge_state, (unsigned)charger_error,
    //          (int)battery_voltage, (int)battery_charging_current,
    //          (unsigned)yield_today, (unsigned)solar_power, (unsigned)external_device_load);

    data->battery_voltage = (battery_voltage == 0x7FFF) ? NAN : (battery_voltage / 100.0f);
    data->battery_charging_current =
        (battery_charging_current == 0x7FFF) ? NAN : (battery_charging_current / 10.0f);
    data->yield_today = (yield_today == 0xFFFF) ? NAN : (yield_today * 10.0f);
    data->solar_power = (solar_power == 0xFFFF) ? NAN : (float)solar_power;
    data->external_device_load =
        (external_device_load == 0x1FF) ? NAN : (external_device_load / 10.0f);

    data->valid = true;

    return true;
}

static bool victron_values_plausible(const victron_mppt_data_t *data)
{
    if (!data->valid) {
        return false;
    }
    if (!isnan(data->battery_voltage)) {
        if (data->battery_voltage < 5.0f || data->battery_voltage > 100.0f) {
            return false;
        }
    }
    return true;
}

bool victron_parser_parse(const uint8_t *decrypted, size_t len, victron_mppt_data_t *data)
{
    if (victron_parser_parse_with_mode(decrypted, len, data, false)) {
        if (victron_values_plausible(data)) {
            return true;
        }
    }

    victron_mppt_data_t alt;
    if (victron_parser_parse_with_mode(decrypted, len, &alt, true)) {
        if (victron_values_plausible(&alt)) {
            *data = alt;
            ESP_LOGW(TAG_PARSER, "Parser MSB usado por plausibilidad");
            return true;
        }
    }

    return false;
}

void victron_parser_print(const victron_mppt_data_t *data)
{
    if (!data) {
        return;
    }

    if (!data->valid) {
        ESP_LOGW(TAG_PARSER, "Datos MPPT no válidos");
        return;
    }

    ESP_LOGI(TAG_PARSER, "Model ID       : 0x%04x", data->model_id);
    ESP_LOGI(TAG_PARSER, "Readout Type   : 0x%02x", data->readout_type);
    ESP_LOGI(TAG_PARSER, "IV             : 0x%06x", (unsigned)data->iv);

    ESP_LOGI(TAG_PARSER, "Estado de carga : %s (%d)",
             victron_parser_charge_state_str(data->charge_state),
             (int)data->charge_state);

    ESP_LOGI(TAG_PARSER, "Error cargador  : %s (%d)",
             victron_parser_error_str(data->charger_error),
             (int)data->charger_error);

    log_float("Batería (V)", data->battery_voltage, "V");
    log_float("Batería (A)", data->battery_charging_current, "A");
    log_float("Panel solar", data->solar_power, "W");
    log_float("Energía hoy", data->yield_today, "Wh");
    log_float("Carga externa", data->external_device_load, "A");
}

const char *victron_parser_charge_state_str(victron_charge_state_t state)
{
    switch (state) {
    case VICTRON_MODE_OFF:               return "Off";
    case VICTRON_MODE_LOW_POWER:         return "Low power";
    case VICTRON_MODE_FAULT:             return "Fault";
    case VICTRON_MODE_BULK:              return "Bulk";
    case VICTRON_MODE_ABSORPTION:        return "Absorption";
    case VICTRON_MODE_FLOAT:             return "Float";
    case VICTRON_MODE_STORAGE:           return "Storage";
    case VICTRON_MODE_EQUALIZE_MANUAL:   return "Equalize (manual)";
    case VICTRON_MODE_INVERTING:         return "Inverting";
    case VICTRON_MODE_POWER_SUPPLY:      return "Power supply";
    case VICTRON_MODE_STARTING_UP:       return "Starting up";
    case VICTRON_MODE_REPEATED_ABSORPTION:return "Repeated absorption";
    case VICTRON_MODE_RECONDITION:       return "Recondition";
    case VICTRON_MODE_BATTERY_SAFE:      return "Battery safe";
    case VICTRON_MODE_ACTIVE:            return "Active";
    case VICTRON_MODE_EXTERNAL_CONTROL:  return "External control";
    case VICTRON_MODE_NOT_AVAILABLE:     return "N/A";
    default:                             return "Desconocido";
    }
}

const char *victron_parser_error_str(victron_charger_error_t error)
{
    switch (error) {
    case VICTRON_ERROR_NO_ERROR:                    return "Sin error";
    case VICTRON_ERROR_TEMPERATURE_BATTERY_HIGH:    return "Batería: temperatura alta";
    case VICTRON_ERROR_VOLTAGE_HIGH:                return "Voltaje alto";
    case VICTRON_ERROR_REMOTE_TEMPERATURE_A:        return "Error temp. remota A";
    case VICTRON_ERROR_REMOTE_TEMPERATURE_B:        return "Error temp. remota B";
    case VICTRON_ERROR_REMOTE_TEMPERATURE_C:        return "Error temp. remota C";
    case VICTRON_ERROR_REMOTE_BATTERY_A:            return "Error batería remota A";
    case VICTRON_ERROR_REMOTE_BATTERY_B:            return "Error batería remota B";
    case VICTRON_ERROR_REMOTE_BATTERY_C:            return "Error batería remota C";
    case VICTRON_ERROR_HIGH_RIPPLE:                 return "Ripple alto";
    case VICTRON_ERROR_TEMPERATURE_BATTERY_LOW:     return "Batería: temperatura baja";
    case VICTRON_ERROR_TEMPERATURE_CHARGER:         return "Cargador: temperatura";
    case VICTRON_ERROR_OVER_CURRENT:                return "Sobre-corriente";
    case VICTRON_ERROR_BULK_TIME:                   return "Tiempo en bulk excedido";
    case VICTRON_ERROR_CURRENT_SENSOR:              return "Sensor de corriente";
    case VICTRON_ERROR_INTERNAL_TEMPERATURE_A:      return "Temp. interna A";
    case VICTRON_ERROR_INTERNAL_TEMPERATURE_B:      return "Temp. interna B";
    case VICTRON_ERROR_FAN:                         return "Ventilador";
    case VICTRON_ERROR_OVERHEATED:                  return "Sobrecalentado";
    case VICTRON_ERROR_SHORT_CIRCUIT:               return "Cortocircuito";
    case VICTRON_ERROR_CONVERTER_ISSUE:             return "Problema en convertidor";
    case VICTRON_ERROR_OVER_CHARGE:                 return "Sobre-carga";
    case VICTRON_ERROR_INPUT_VOLTAGE:               return "Voltaje de entrada";
    case VICTRON_ERROR_INPUT_CURRENT:               return "Corriente de entrada";
    case VICTRON_ERROR_INPUT_POWER:                 return "Potencia de entrada";
    case VICTRON_ERROR_INPUT_SHUTDOWN_VOLTAGE:      return "Apagado por V entrada";
    case VICTRON_ERROR_INPUT_SHUTDOWN_CURRENT:      return "Apagado por I entrada";
    case VICTRON_ERROR_INPUT_SHUTDOWN_FAILURE:      return "Fallo apagado entrada";
    case VICTRON_ERROR_UNKNOWN:                     return "Desconocido";
    default:                                        return "No documentado";
    }
}

