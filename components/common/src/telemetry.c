/**
 * @file telemetry.c
 * @brief InfluxDB line-protocol telemetry client (see telemetry.h).
 */
#include "telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_mac.h"

#include "node_config.h"

static const char *TAG = "telemetry";
static uint32_t s_system_error_counter = 0;

static int get_mac_str(char *buf)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        ESP_LOGE(TAG, "esp_read_mac() failed");
        buf[0] = '\0';
        return -1;
    }
    return snprintf(buf, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static esp_err_t post_line(const char *body, size_t body_len)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/api/v2/write?org=%s&bucket=%s&precision=s",
             NODE_INFLUX_URL, NODE_INFLUX_ORG, NODE_INFLUX_BUCKET);

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Token %s", NODE_INFLUX_TOKEN);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_post_field(client, body, (int)body_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Influx POST err 0x%x", err);
    }

    esp_http_client_cleanup(client);
    return err;
}

/**
 * Builds an Influx line-protocol body from fmt/args (sized buffer allocated
 * on the heap, freed before returning) and POSTs it. This one function
 * replaces the three copy-pasted malloc+snprintf+POST blocks that used to
 * live in metrics_task.c, one per measurement.
 */
static esp_err_t send_line_v(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    va_list ap_size;
    va_copy(ap_size, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap_size);
    va_end(ap_size);

    if (needed < 0) {
        va_end(ap);
        ESP_LOGE(TAG, "vsnprintf size calc failed");
        return ESP_FAIL;
    }

    size_t buf_sz = (size_t)needed + 1;
    char *body = malloc(buf_sz);
    if (!body) {
        va_end(ap);
        ESP_LOGE(TAG, "malloc(%u) failed", (unsigned)buf_sz);
        return ESP_ERR_NO_MEM;
    }

    int written = vsnprintf(body, buf_sz, fmt, ap);
    va_end(ap);

    if (written < 0 || (size_t)written >= buf_sz) {
        free(body);
        ESP_LOGE(TAG, "vsnprintf write failed/truncated");
        return ESP_FAIL;
    }

    esp_err_t err = post_line(body, (size_t)written);
    free(body);
    return err;
}

esp_err_t telemetry_send_victron_mppt(int charge_state, int charger_error,
                                       float batt_v, float batt_a, float yield_wh,
                                       float solar_w, float load_a)
{
    char mac_str[18];
    get_mac_str(mac_str);

    esp_err_t err = send_line_v(
        "victron,mac=%s,loc=%s,role=%s charge_state=%di,charger_error=%di,"
        "batt_v=%.2f,batt_a=%.2f,yield_wh=%.1f,solar_w=%.1f,load_a=%.2f\n",
        mac_str, SITE_LOCATION_TAG, NODE_ROLE_TAG, charge_state, charger_error,
        batt_v, batt_a, yield_wh, solar_w, load_a);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "victron metrics sent");
    }
    return err;
}

esp_err_t telemetry_send_bme280(float temp_c, float hum_pct, float press_hpa)
{
    char mac_str[18];
    get_mac_str(mac_str);

    esp_err_t err = send_line_v(
        "bme280,mac=%s,loc=%s,role=%s temp_c=%.2f,hum=%.2f,press_hpa=%.2f\n",
        mac_str, SITE_LOCATION_TAG, NODE_ROLE_TAG, temp_c, hum_pct, press_hpa);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "bme280 metrics sent");
    }
    return err;
}

esp_err_t telemetry_send_system(void)
{
    char mac_str[18];
    get_mac_str(mac_str);

    uint32_t heap_size = esp_get_free_heap_size();
    int64_t uptime_sec = esp_timer_get_time() / 1000000;

    esp_err_t err = send_line_v(
        "system,mac=%s,loc=%s,role=%s heap_free=%lu,uptime_sec=%lld,sys_error_counter=%lu\n",
        mac_str, SITE_LOCATION_TAG, NODE_ROLE_TAG, (unsigned long)heap_size,
        (long long)uptime_sec, (unsigned long)s_system_error_counter);

    if (err == ESP_OK) {
        s_system_error_counter = 0;
        ESP_LOGI(TAG, "system metrics sent");
    } else if (s_system_error_counter != UINT32_MAX) {
        s_system_error_counter++;
    }
    return err;
}
