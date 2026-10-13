/**
 * @file lora_rx.c
 * @brief See lora_rx.h.
 */
#include "lora_rx.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"

#include "common.h"
#include "sx1276.h"
#include "lora_proto.h"
#include "telemetry.h"
#include "ota_lora_gw.h"

static const char *TAG = "lora_rx";

#define LORA_RX_TASK_STACK  (3 * APP_MINIMAL_STACK_SIZE)
#define STORE_MAGIC         0x51C0A7E1u
#define MAX_NODES_TRACKED   8

typedef struct {
    uint32_t             id;            // monotonically increasing
    uint32_t             rx_boot;       // gateway boot count at reception
    uint32_t             rx_uptime_s;
    int16_t              rssi_dbm;
    int8_t               snr_q4;        // SNR * 4
    lora_msg_telemetry_t msg;
} stored_reading_t;

typedef struct {
    uint32_t         magic;
    uint32_t         boot_count;
    uint32_t         next_id;
    uint16_t         head;              // index of the oldest reading
    uint16_t         count;
    stored_reading_t r[LORA_RX_BUFFER_DEPTH];
    uint32_t         crc;               // over everything above
} rtc_store_t;

// Survives esp_restart()/panic/WDT resets; garbage after power-on (detected
// via magic + CRC and reset).
static RTC_NOINIT_ATTR rtc_store_t s_store;
static SemaphoreHandle_t s_mutex;
static uint32_t s_boot;

// Last telemetry seq per node, to drop retransmissions whose ACK got lost.
static struct { uint8_t node; uint16_t seq; bool used; } s_last_seq[MAX_NODES_TRACKED];

static uint32_t store_crc(void)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&s_store, offsetof(rtc_store_t, crc));
}

static void store_seal(void)
{
    s_store.crc = store_crc();
}

static void store_init(void)
{
    if (s_store.magic != STORE_MAGIC || s_store.crc != store_crc() ||
        s_store.count > LORA_RX_BUFFER_DEPTH || s_store.head >= LORA_RX_BUFFER_DEPTH) {
        memset(&s_store, 0, sizeof(s_store));
        s_store.magic = STORE_MAGIC;
        ESP_LOGI(TAG, "RTC buffer initialized (power-on or corrupt)");
    } else if (s_store.count) {
        ESP_LOGI(TAG, "RTC buffer kept %u readings from before the reboot", s_store.count);
    }
    s_boot = ++s_store.boot_count;
    store_seal();
}

static void store_push(const lora_msg_telemetry_t *msg, int16_t rssi, float snr)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint16_t idx;
    if (s_store.count == LORA_RX_BUFFER_DEPTH) {
        // Full: overwrite the oldest one.
        idx = s_store.head;
        s_store.head = (s_store.head + 1) % LORA_RX_BUFFER_DEPTH;
        ESP_LOGW(TAG, "Buffer full, dropping oldest reading");
    } else {
        idx = (s_store.head + s_store.count) % LORA_RX_BUFFER_DEPTH;
        s_store.count++;
    }
    stored_reading_t *r = &s_store.r[idx];
    r->id = s_store.next_id++;
    r->rx_boot = s_boot;
    r->rx_uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    r->rssi_dbm = rssi;
    r->snr_q4 = (int8_t)(snr * 4);
    r->msg = *msg;
    store_seal();
    xSemaphoreGive(s_mutex);
}

size_t lora_rx_pending(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    size_t n = s_store.count;
    xSemaphoreGive(s_mutex);
    return n;
}

int lora_rx_flush_to_telemetry(void)
{
    int sent = 0;
    while (1) {
        stored_reading_t r;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_store.count == 0) {
            xSemaphoreGive(s_mutex);
            break;
        }
        r = s_store.r[s_store.head];
        xSemaphoreGive(s_mutex);

        // HTTP POST without holding the mutex: the lora task must be able to
        // store (and ACK) new frames meanwhile.
        int32_t age = (r.rx_boot == s_boot)
                    ? (int32_t)((esp_timer_get_time() / 1000000) - r.rx_uptime_s) : -1;
        if (telemetry_send_remote_weather(&r.msg, r.rssi_dbm, r.snr_q4 / 4.0f, age) != ESP_OK) {
            ESP_LOGW(TAG, "InfluxDB POST failed, %u readings stay buffered", (unsigned)lora_rx_pending());
            break;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        // Pop only if it's still the same one (it could have been overwritten
        // by a push while the buffer was full).
        if (s_store.count && s_store.r[s_store.head].id == r.id) {
            s_store.head = (s_store.head + 1) % LORA_RX_BUFFER_DEPTH;
            s_store.count--;
            store_seal();
        }
        xSemaphoreGive(s_mutex);
        sent++;
    }
    if (sent) {
        ESP_LOGI(TAG, "Forwarded %d remote readings to InfluxDB", sent);
    }
    return sent;
}

static bool is_duplicate(uint8_t node, uint16_t seq)
{
    int free_slot = -1;
    for (int i = 0; i < MAX_NODES_TRACKED; i++) {
        if (s_last_seq[i].used && s_last_seq[i].node == node) {
            if (s_last_seq[i].seq == seq) {
                return true;
            }
            s_last_seq[i].seq = seq;
            return false;
        }
        if (!s_last_seq[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot >= 0) {
        s_last_seq[free_slot].used = true;
        s_last_seq[free_slot].node = node;
        s_last_seq[free_slot].seq = seq;
    }
    return false;
}

static void handle_telemetry(const uint8_t *buf, uint8_t len, int16_t rssi, float snr)
{
    if (len < sizeof(lora_msg_telemetry_t)) {
        return;
    }
    lora_msg_telemetry_t msg;
    memcpy(&msg, buf, sizeof(msg));

    char remote_version[LORA_FW_VERSION_LEN + 1];
    memcpy(remote_version, msg.fw_version, LORA_FW_VERSION_LEN);
    remote_version[LORA_FW_VERSION_LEN] = '\0';

    // 1) ACK first: the remote is only listening for a few hundred ms.
    lora_msg_telemetry_ack_t ack = { 0 };
    lora_hdr_fill(&ack.hdr, LORA_MSG_TELEMETRY_ACK, LORA_GATEWAY_ID, msg.hdr.src, msg.hdr.seq);
    ack.ota_offer = ota_lora_gw_should_offer(remote_version, ack.ota_version) ? 1 : 0;
    esp_err_t err = sx1276_send_packet((const uint8_t *)&ack, sizeof(ack));

    // 2) Then store it (unless it's a retransmission of one already stored).
    const bool dup = is_duplicate(msg.hdr.src, msg.hdr.seq);
    if (!dup) {
        store_push(&msg, rssi, snr);
    }

    ESP_LOGI(TAG, "Telemetry from node %u seq %u (fw %s, RSSI %d dBm, SNR %.1f dB)%s, ACK %s%s",
             msg.hdr.src, msg.hdr.seq, remote_version, rssi, snr, dup ? " [dup]" : "",
             err == ESP_OK ? "sent" : esp_err_to_name(err), ack.ota_offer ? " + OTA offer" : "");
}

static void handle_ota_request(const uint8_t *buf, uint8_t len)
{
    if (len < sizeof(lora_msg_ota_request_t)) {
        return;
    }
    lora_msg_ota_request_t req;
    memcpy(&req, buf, sizeof(req));
    req.version[LORA_FW_VERSION_LEN - 1] = '\0';

    ESP_LOGI(TAG, "Node %u requests OTA to %s", req.hdr.src, req.version);
    esp_err_t err = ota_lora_gw_serve(req.hdr.src, req.version);
    ESP_LOGI(TAG, "OTA session with node %u ended: %s", req.hdr.src, esp_err_to_name(err));
}

static void lora_rx_task(void *arg)
{
    (void)arg;
    uint8_t buf[LORA_MAX_PAYLOAD];
    uint8_t len;
    int16_t rssi;
    float snr;

    while (1) {
        sx1276_start_receive();
        if (sx1276_receive_packet(buf, &len, &rssi, &snr, 0) != ESP_OK) {
            continue;
        }
        lora_hdr_t hdr;
        if (!lora_hdr_is_for(buf, len, LORA_GATEWAY_ID, &hdr)) {
            continue;   // other networks, other protocol versions, other dst
        }
        switch (hdr.type) {
        case LORA_MSG_TELEMETRY:
            handle_telemetry(buf, len, rssi, snr);
            break;
        case LORA_MSG_OTA_REQUEST:
            handle_ota_request(buf, len);
            break;
        default:
            break;  // stray OTA_ACK/NACK from an old session, etc.
        }
    }
}

esp_err_t lora_rx_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    store_init();

    ESP_RETURN_ON_ERROR(sx1276_init(), TAG, "sx1276_init() failed");
    ESP_RETURN_ON_ERROR(ota_lora_gw_init(), TAG, "ota_lora_gw_init() failed");

    if (xTaskCreate(&lora_rx_task, "lora_rx_task", LORA_RX_TASK_STACK, NULL, HIGH_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
