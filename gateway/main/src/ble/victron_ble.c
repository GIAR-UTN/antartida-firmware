/**
 * @file victron_ble.c
 * @brief Implementación del módulo BLE para conexión con MPPT Victron
 * 
 * Flujo:
 * 1. Scan → Buscar dispositivo por MAC address
 * 2. Connect → Conectar como cliente GATT
 * 3. Discover → Descubrir servicios y características
 * 4. Subscribe → Suscribirse a notifications
 * 5. Notify → Recibir payloads cifrados
 */

#include "victron_ble.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"

static const char *TAG = "VICTRON_BLE";

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static void victron_ble_host_task(void *param);
static void victron_ble_host_task(void *param);

// Estado del módulo BLE
static struct {
    bool initialized;
    bool scanning;
    bool connected;
    char target_mac[18];  // MAC address del dispositivo objetivo
    uint16_t conn_handle;
    uint16_t char_val_handle;  // Handle de la característica para notifications
    victron_ble_data_cb_t data_callback;
    uint8_t own_addr_type;
    uint32_t disc_count;
    bool seen_target;
    int64_t last_adv_log_us;
} ble_state = {0};

/**
 * @brief Convertir MAC address string a bytes
 */
static int mac_str_to_bytes(const char *mac_str, uint8_t *mac_bytes) {
    int values[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        mac_bytes[i] = (uint8_t)values[i];
    }
    return 0;
}

/**
 * @brief Comparar MAC addresses
 */
static bool mac_match(const uint8_t *mac1, const uint8_t *mac2) {
    return memcmp(mac1, mac2, 6) == 0;
}

/**
 * @brief Callback de sincronización BLE
 */
static void ble_on_sync(void) {
    int rc = ble_hs_id_infer_auto(0, &ble_state.own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "No se pudo inferir addr_type: %d", rc);
        ble_state.own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }
    ESP_LOGI(TAG, "BLE sincronizado (own_addr_type=%d)", ble_state.own_addr_type);
}

/**
 * @brief Callback de reset BLE
 */
static void ble_on_reset(int reason) {
    ESP_LOGE(TAG, "BLE reset, razón: %d", reason);
    ble_state.connected = false;
    ble_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

/**
 * @brief Callback de escaneo - cuando se encuentra un dispositivo
 */
static void ble_scan_cb(const struct ble_gap_event *event, void *arg) {
    const struct ble_gap_disc_desc *disc = &event->disc;
    uint8_t target_mac[6];
    uint8_t target_mac_le[6];
    
    if (event->type != BLE_GAP_EVENT_DISC) {
        return;
    }

    ble_state.disc_count++;

    // Convertir MAC objetivo a bytes
    if (mac_str_to_bytes(ble_state.target_mac, target_mac) != 0) {
        return;
    }

    for (int i = 0; i < 6; i++) {
        target_mac_le[i] = target_mac[5 - i];
    }

    // Verificar si es el dispositivo objetivo
    if (!mac_match(disc->addr.val, target_mac_le)) {
        return;
    }

    ble_state.seen_target = true;
    int64_t now_us = esp_timer_get_time();
    // bool should_log = (ble_state.last_adv_log_us == 0) ||
    //                   (now_us - ble_state.last_adv_log_us) >= 10000000LL;
    // if (should_log) {
    //     ble_state.last_adv_log_us = now_us;
    //     ESP_LOGI(TAG, "Dispositivo encontrado: %02x:%02x:%02x:%02x:%02x:%02x",
    //              disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
    //              disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
    //     ESP_LOGI(TAG, "RSSI: %d dBm", disc->rssi);
    // }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    int rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0 || fields.mfg_data_len < 2) {
        return;
    }

    uint16_t company_id = (uint16_t)(fields.mfg_data[0] | ((uint16_t)fields.mfg_data[1] << 8));
    if (company_id != 0x02E1) {
        return;
    }

    const uint8_t *payload = fields.mfg_data + 2;
    uint16_t payload_len = (uint16_t)(fields.mfg_data_len - 2);
    if (payload_len == 0) {
        return;
    }

    // if (should_log) {
    //     ESP_LOGI(TAG, "ADV Victron (%d bytes)", (int)payload_len);
    //     ESP_LOG_BUFFER_HEX(TAG, payload, payload_len);
    // }

    if (ble_state.data_callback != NULL) {
        ble_state.data_callback(payload, payload_len);
    }
}

/**
 * @brief Callback de eventos GAP
 */
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
        case BLE_GAP_EVENT_DISCONNECT:
            return 0;

        case BLE_GAP_EVENT_DISC:
            ble_scan_cb(event, arg);
            return 0;

        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Escaneo completado (%u dispositivos, target %s)",
                     (unsigned)ble_state.disc_count,
                     ble_state.seen_target ? "visto" : "no visto");
            ble_state.scanning = false;
            return 0;
        case BLE_GAP_EVENT_NOTIFY_RX:
            return 0;

        default:
            return 0;
    }
}

/**
 * @brief Inicializar BLE
 */
esp_err_t victron_ble_init(void) {
    if (ble_state.initialized) {
        ESP_LOGW(TAG, "BLE ya inicializado");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando BLE...");

    // Inicializar controlador + host NimBLE
    esp_err_t rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando NimBLE: %d", rc);
        return ESP_FAIL;
    }

    // Configurar callbacks
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;

    // Iniciar task de NimBLE
    nimble_port_freertos_init(victron_ble_host_task);

    ble_state.initialized = true;
    ESP_LOGI(TAG, "BLE inicializado correctamente");

    return ESP_OK;
}

/**
 * @brief Iniciar escaneo
 */
esp_err_t victron_ble_scan_start(const char *mac_addr) {
    if (!ble_state.initialized) {
        ESP_LOGE(TAG, "BLE no inicializado");
        return ESP_ERR_INVALID_STATE;
    }

    if (ble_state.scanning) {
        ESP_LOGW(TAG, "Escaneo ya activo, se omite nuevo inicio");
        return ESP_ERR_INVALID_STATE;
    }

    if (mac_addr != NULL) {
        strncpy(ble_state.target_mac, mac_addr, sizeof(ble_state.target_mac) - 1);
        ble_state.target_mac[sizeof(ble_state.target_mac) - 1] = '\0';
    }

    ble_state.disc_count = 0;
    ble_state.seen_target = false;
    ESP_LOGI(TAG, "Iniciando escaneo BLE, buscando: %s", ble_state.target_mac);

    struct ble_gap_disc_params disc_params = {0};
    disc_params.filter_duplicates = 0;
    disc_params.passive = 1;
    disc_params.itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN;
    disc_params.window = BLE_GAP_SCAN_FAST_WINDOW;

    int rc = ble_gap_disc(ble_state.own_addr_type, BLE_HS_FOREVER, &disc_params,
                         ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error iniciando escaneo: %d", rc);
        return ESP_FAIL;
    }

    ble_state.scanning = true;
    return ESP_OK;
}

/**
 * @brief Detener escaneo
 */
esp_err_t victron_ble_scan_stop(void) {
    if (ble_state.scanning) {
        ble_gap_disc_cancel();
        ble_state.scanning = false;
    }
    return ESP_OK;
}

/**
 * @brief Conectar al dispositivo
 */
esp_err_t victron_ble_connect(const char *mac_addr) {
    // La conexión se maneja automáticamente en el callback de escaneo
    return victron_ble_scan_start(mac_addr);
}

/**
 * @brief Desconectar
 */
esp_err_t victron_ble_disconnect(void) {
    if (!ble_state.connected) {
        return ESP_OK;
    }

    int rc = ble_gap_terminate(ble_state.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error desconectando: %d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Verificar si está conectado
 */
bool victron_ble_is_connected(void) {
    return ble_state.connected;
}

bool victron_ble_is_scanning(void) {
    return ble_state.scanning;
}

/**
 * @brief Registrar callback de datos
 */
void victron_ble_register_data_callback(victron_ble_data_cb_t cb) {
    ble_state.data_callback = cb;
}

/**
 * @brief Deinicializar BLE
 */
void victron_ble_deinit(void) {
    if (ble_state.connected) {
        victron_ble_disconnect();
    }
    if (ble_state.scanning) {
        victron_ble_scan_stop();
    }
    nimble_port_freertos_deinit();
    ble_state.initialized = false;
}

/**
 * @brief Task del host BLE (requerido por NimBLE)
 */
static void victron_ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task iniciado");
    nimble_port_run();
    nimble_port_freertos_deinit();
}
