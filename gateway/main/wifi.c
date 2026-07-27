
#include "wifi.h"

static const char *TAG = "wifi.c";

// -------------------
static esp_netif_t *s_example_sta_netif = NULL;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;
static SemaphoreHandle_t s_semph_get_ip6_addrs = NULL;
static int s_retry_num = 0;



void custom_handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    s_retry_num++;
    if (s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
        ESP_LOGI(TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let custom_wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs) {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }
        if (s_semph_get_ip6_addrs) {
            xSemaphoreGive(s_semph_get_ip6_addrs);
        }

        custom_wifi_sta_do_disconnect();
        return;
    }
    wifi_event_sta_disconnected_t *disconn = event_data;
    if (disconn->reason == WIFI_REASON_ROAMING) {
        ESP_LOGD(TAG, "station roaming, do nothing");
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected %d, trying to reconnect...", disconn->reason);
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

void custom_handler_on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
#if CONFIG_EXAMPLE_CONNECT_IPV6
    esp_netif_create_ip6_linklocal(esp_netif);
#endif // CONFIG_EXAMPLE_CONNECT_IPV6
}

void custom_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!example_is_our_netif("custom_netif_sta", event->esp_netif)) {
        return;
    }
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    if (s_semph_get_ip_addrs) {
        xSemaphoreGive(s_semph_get_ip_addrs);
    } else {
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
}

#if CONFIG_EXAMPLE_CONNECT_IPV6
void custom_handler_on_sta_got_ipv6(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ip_event_got_ip6_t *event = (ip_event_got_ip6_t *)event_data;
    if (!example_is_our_netif("custom_netif_sta", event->esp_netif)) {
        return;
    }
    esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&event->ip6_info.ip);
    ESP_LOGI(TAG, "Got IPv6 event: Interface \"%s\" address: " IPV6STR ", type: %s", esp_netif_get_desc(event->esp_netif),
             IPV62STR(event->ip6_info.ip), example_ipv6_addr_types_to_str[ipv6_type]);

    if (ipv6_type == EXAMPLE_CONNECT_PREFERRED_IPV6_TYPE) {
        if (s_semph_get_ip6_addrs) {
            xSemaphoreGive(s_semph_get_ip6_addrs);
        } else {
            ESP_LOGI(TAG, "- IPv6 address: " IPV6STR ", type: %s", IPV62STR(event->ip6_info.ip), example_ipv6_addr_types_to_str[ipv6_type]);
        }
    }
}
#endif // CONFIG_EXAMPLE_CONNECT_IPV6

void custom_wifi_start(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = "custom_netif_sta";
    esp_netif_config.route_prio = 128;
    s_example_sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void custom_wifi_stop(void)
{
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(s_example_sta_netif));
    esp_netif_destroy(s_example_sta_netif);
    s_example_sta_netif = NULL;
}

esp_err_t custom_wifi_sta_do_connect(wifi_config_t wifi_config, bool wait)
{
    if (wait) {
        s_semph_get_ip_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip_addrs == NULL) {
            return ESP_ERR_NO_MEM;
        }
#if CONFIG_EXAMPLE_CONNECT_IPV6
        s_semph_get_ip6_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip6_addrs == NULL) {
            vSemaphoreDelete(s_semph_get_ip_addrs);
            return ESP_ERR_NO_MEM;
        }
#endif
    }
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &custom_handler_on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &custom_handler_on_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &custom_handler_on_wifi_connect, s_example_sta_netif));
#if CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &custom_handler_on_sta_got_ipv6, NULL));
#endif

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }
    if (wait) {
        ESP_LOGI(TAG, "Waiting for IP(s)");
#if CONFIG_EXAMPLE_CONNECT_IPV4
        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
        vSemaphoreDelete(s_semph_get_ip_addrs);
        s_semph_get_ip_addrs = NULL;
#endif
#if CONFIG_EXAMPLE_CONNECT_IPV6
        xSemaphoreTake(s_semph_get_ip6_addrs, portMAX_DELAY);
        vSemaphoreDelete(s_semph_get_ip6_addrs);
        s_semph_get_ip6_addrs = NULL;
#endif
        if (s_retry_num > CONFIG_EXAMPLE_WIFI_CONN_MAX_RETRY) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t custom_wifi_sta_do_disconnect(void)
{
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &custom_handler_on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &custom_handler_on_sta_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &custom_handler_on_wifi_connect));
#if CONFIG_EXAMPLE_CONNECT_IPV6
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_GOT_IP6, &custom_handler_on_sta_got_ipv6));
#endif
    return esp_wifi_disconnect();
}

void custom_wifi_shutdown(void)
{
    custom_wifi_sta_do_disconnect();
    custom_wifi_stop();
}

esp_err_t custom_wifi_connect(void)
{
    ESP_LOGI(TAG, "Start example_connect.");
    custom_wifi_start();
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = NODE_WIFI_SSID,
            .password = NODE_WIFI_PASSWORD,
            .scan_method = EXAMPLE_WIFI_SCAN_METHOD,
            .sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD,
            .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

    return custom_wifi_sta_do_connect(wifi_config, true);
}

esp_err_t print_all_ips_tcpip(void* ctx)
{
    const char *prefix = ctx;
    // iterate over active interfaces, and print out IPs of "our" netifs
    esp_netif_t *netif = NULL;
    while ((netif = esp_netif_next_unsafe(netif)) != NULL) {
        if (example_is_our_netif(prefix, netif)) {
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
#if CONFIG_EXAMPLE_CONNECT_IPV4
            esp_netif_ip_info_t ip;
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));

            ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&ip.ip));
#endif
#if CONFIG_EXAMPLE_CONNECT_IPV6
            esp_ip6_addr_t ip6[MAX_IP6_ADDRS_PER_NETIF];
            int ip6_addrs = esp_netif_get_all_ip6(netif, ip6);
            for (int j = 0; j < ip6_addrs; ++j) {
                esp_ip6_addr_type_t ipv6_type = esp_netif_ip6_get_addr_type(&(ip6[j]));
                ESP_LOGI(TAG, "- IPv6 address: " IPV6STR ", type: %s", IPV62STR(ip6[j]), example_ipv6_addr_types_to_str[ipv6_type]);
            }
#endif
        }
    }
    return ESP_OK;
}

esp_err_t custom_connect(void)
{
    if (custom_wifi_connect() != ESP_OK) {
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&custom_wifi_shutdown));

    print_all_ips_tcpip("custom_netif_sta");

    return ESP_OK;
}


// -----



/**
 * @brief Verifica si la STA está conectada a un AP y (opcionalmente) tiene IPv4 asignada.
 *
 * @return true  Si hay conexión establecida
 * @return false Si no hay conexión o no está lista
 */
bool wifi_is_connected(void)
{
    // Si el netif todavía no existe, no hubo init/start válido
    if (s_example_sta_netif == NULL) {
        return false;
    }

    // 1) Verifica asociación WiFi (STA conectada al AP)
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK) {
        return false; // no asociado o WiFi no listo
    }

#if CONFIG_EXAMPLE_CONNECT_IPV4
    // 2) Verifica que tenga IPv4 válida (más estricto que "asociado")
    esp_netif_ip_info_t ip_info;
    err = esp_netif_get_ip_info(s_example_sta_netif, &ip_info);
    if (err != ESP_OK) {
        return false;
    }

    if (ip_info.ip.addr == 0) {
        return false; // asociado pero aún sin IP
    }
#endif

    return true;
}

/**
 * @brief Si no hay conexión WiFi establecida, intenta conectarse.
 *
 * @note Asume que wifi_init() (o al menos custom_wifi_start()/esp_netif_init()) ya se ejecutó.
 *       No reinicializa el driver ni el netif.
 *
 * @return ESP_OK   Si ya estaba conectado o si conectó correctamente
 * @return ESP_FAIL / error de ESP-IDF en caso de falla
 */
esp_err_t wifi_connect_if_needed(void)
{
    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "WiFi ya estaba conectado.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "WiFi no conectado. Intentando conectar...");

    // Reutiliza la misma config que tu custom_wifi_connect()
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = NODE_WIFI_SSID,
            .password = NODE_WIFI_PASSWORD,
            .scan_method = EXAMPLE_WIFI_SCAN_METHOD,
            .sort_method = EXAMPLE_WIFI_CONNECT_AP_SORT_METHOD,
            .threshold.rssi = CONFIG_EXAMPLE_WIFI_SCAN_RSSI_THRESHOLD,
            .threshold.authmode = EXAMPLE_WIFI_SCAN_AUTH_MODE_THRESHOLD,
        },
    };

    esp_err_t err = custom_wifi_sta_do_connect(wifi_config, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo establecer la conexión WiFi. err=0x%x", err);
        return err;
    }

    // Mantengo tu decisión de desactivar power save
    esp_wifi_set_ps(WIFI_PS_NONE);

    return ESP_OK;
}

/**
 * @brief Termina una conexión WiFi existente (desconecta de la red).
 *
 * @note No apaga el driver WiFi ni destruye netif (para eso usar custom_wifi_shutdown()).
 *
 * @return ESP_OK si desconectó o si ya no había conexión
 * @return error en caso de falla real
 */
esp_err_t wifi_disconnect_if_connected(void)
{
    if (!wifi_is_connected()) {
        ESP_LOGI(TAG, "No hay conexión WiFi activa para terminar.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Terminando conexión WiFi...");

    // Usa tu función existente: desregistra handlers + esp_wifi_disconnect()
    return custom_wifi_sta_do_disconnect();
}


esp_err_t wifi_init(void)
{
    /**
     * @brief Initialize WiFi. Connects to selected SSID network.
     *
     * @param None  None.
     * @return esp_err_t
     *         - ESP_OK on success
     *         - Otherwise returns ESP_FAIL.
     */
    esp_err_t err = ESP_OK;

    esp_log_level_set("wifi", ESP_LOG_ERROR);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init() failed.");

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_RETURN_ON_ERROR(custom_connect(), TAG, "example_connect() failed.");
    

    /* Ensure to disable any WiFi power save mode, this allows best throughput
     * and hence timings for overall OTA operation.
     */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "esp_wifi_set_ps() failed.");

    return err;
}