/**
 * @file ota_lora_gw.c
 * @brief See ota_lora_gw.h. Based on Aylen's ota_lora_gateway.c.
 */
#include "ota_lora_gw.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_image_format.h"
#include "esp_rom_crc.h"
#include "sx1276.h"

static const char *TAG = "ota_lora_gw";

typedef struct {
    const esp_partition_t *part;
    bool                   valid;
    uint32_t               size;
    uint32_t               crc32;
    char                   version[LORA_FW_VERSION_LEN + 1];
} image_info_t;

static image_info_t s_img;
static SemaphoreHandle_t s_img_mutex;
static volatile bool s_busy;
static volatile bool s_hold;

const esp_partition_t *ota_lora_gw_partition(void)
{
    return s_img.part;
}

bool ota_lora_gw_lock(uint32_t timeout_ms)
{
    return s_img_mutex && xSemaphoreTake(s_img_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void ota_lora_gw_unlock(void)
{
    xSemaphoreGive(s_img_mutex);
}

void ota_lora_gw_hold_offers(bool hold)
{
    s_hold = hold;
}

bool ota_lora_gw_busy(void)
{
    return s_busy;
}

void ota_lora_gw_invalidate(void)
{
    s_img.valid = false;
}

esp_err_t ota_lora_gw_reload(void)
{
    s_img.valid = false;
    if (s_img.part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    // Verifies it's a valid ESP-IDF app image and gets its real length.
    esp_image_metadata_t meta = { 0 };
    const esp_partition_pos_t pos = { .offset = s_img.part->address, .size = s_img.part->size };
    esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No valid remote image stored in '%s'", OTA_LORA_IMG_PARTITION);
        return err;
    }

    esp_app_desc_t desc;
    err = esp_partition_read(s_img.part, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t),
                             &desc, sizeof(desc));
    if (err != ESP_OK || desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Stored image has no esp_app_desc_t");
        return ESP_ERR_INVALID_VERSION;
    }

    // CRC32 of the whole image, so the node can check it got exactly these bytes.
    uint8_t buf[256];
    uint32_t crc = 0;
    for (uint32_t off = 0; off < meta.image_len; off += sizeof(buf)) {
        uint32_t n = meta.image_len - off < sizeof(buf) ? meta.image_len - off : sizeof(buf);
        err = esp_partition_read(s_img.part, off, buf, n);
        if (err != ESP_OK) {
            return err;
        }
        crc = esp_rom_crc32_le(crc, buf, n);
    }

    s_img.size = meta.image_len;
    s_img.crc32 = crc;
    memcpy(s_img.version, desc.version, LORA_FW_VERSION_LEN);
    s_img.version[LORA_FW_VERSION_LEN] = '\0';
    s_img.valid = true;

    ESP_LOGI(TAG, "Remote image: version %s, %" PRIu32 " bytes, CRC32 0x%08" PRIx32,
             s_img.version, s_img.size, s_img.crc32);
    return ESP_OK;
}

esp_err_t ota_lora_gw_init(void)
{
    s_img_mutex = xSemaphoreCreateMutex();
    if (s_img_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_img.part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                          OTA_LORA_IMG_PARTITION);
    if (s_img.part == NULL) {
        ESP_LOGW(TAG, "No '%s' partition: OTA over LoRa disabled", OTA_LORA_IMG_PARTITION);
        return ESP_OK;
    }
    ota_lora_gw_reload();
    return ESP_OK;
}

bool ota_lora_gw_image_version(char out[LORA_FW_VERSION_LEN + 1])
{
    if (!s_img.valid) {
        return false;
    }
    memcpy(out, s_img.version, LORA_FW_VERSION_LEN + 1);
    return true;
}

bool ota_lora_gw_should_offer(const char *remote_version, char offer_version[LORA_FW_VERSION_LEN])
{
    // Locked = a download is rewriting the image (or a session is running).
    if (s_busy || s_hold || !ota_lora_gw_lock(0)) {
        return false;
    }
    bool offer = s_img.valid && strncmp(remote_version, s_img.version, LORA_FW_VERSION_LEN) != 0;
    if (offer) {
        memcpy(offer_version, s_img.version, LORA_FW_VERSION_LEN);
    }
    ota_lora_gw_unlock();
    return offer;
}

/*
    Sends a frame and waits for the node's OTA_ACK/OTA_NACK for that session.
    Other frames (telemetry from other nodes, another session) are ignored.
*/
static esp_err_t transact(const void *tx, uint8_t tx_len, uint8_t node, uint16_t session,
                          lora_msg_ota_resp_t *resp, uint32_t timeout_ms)
{
    esp_err_t err = sx1276_send_packet(tx, tx_len);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t buf[LORA_MAX_PAYLOAD];
    uint8_t len;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);

    sx1276_start_receive();
    while (1) {
        int32_t left = (int32_t)(deadline - xTaskGetTickCount());
        if (left <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        uint32_t left_ms = (uint32_t)left * portTICK_PERIOD_MS;
        err = sx1276_receive_packet(buf, &len, NULL, NULL, left_ms ? left_ms : 1);
        if (err == ESP_ERR_TIMEOUT) {
            return err;
        }
        lora_hdr_t hdr;
        if (err != ESP_OK || !lora_hdr_is_for(buf, len, LORA_GATEWAY_ID, &hdr) ||
            hdr.src != node || hdr.seq != session ||
            (hdr.type != LORA_MSG_OTA_ACK && hdr.type != LORA_MSG_OTA_NACK) ||
            len < sizeof(lora_msg_ota_resp_t)) {
            continue;
        }
        memcpy(resp, buf, sizeof(*resp));
        return ESP_OK;
    }
}

static void send_abort(uint8_t node, uint16_t session)
{
    lora_hdr_t msg;
    lora_hdr_fill(&msg, LORA_MSG_OTA_ABORT, LORA_GATEWAY_ID, node, session);
    sx1276_send_packet((const uint8_t *)&msg, sizeof(msg));
}

static esp_err_t run_session(uint8_t node)
{
    const uint8_t chunk = LORA_OTA_CHUNK_SIZE;
    const uint32_t total32 = (s_img.size + chunk - 1) / chunk;
    if (total32 > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    const uint16_t total = (uint16_t)total32;
    const uint16_t session = (uint16_t)(esp_random() & 0xFFFF);
    const uint32_t ack_timeout_ms = lora_reply_window_ms(sizeof(lora_msg_ota_resp_t)) + LORA_FLASH_ERASE_MARGIN_MS;

    lora_msg_ota_resp_t resp;
    esp_err_t err = ESP_FAIL;
    int retries;

    /* ---------- START ---------- */
    lora_msg_ota_start_t start = { 0 };
    lora_hdr_fill(&start.hdr, LORA_MSG_OTA_START, LORA_GATEWAY_ID, node, session);
    start.image_size = s_img.size;
    start.image_crc32 = s_img.crc32;
    start.total_chunks = total;
    start.chunk_size = chunk;
    memcpy(start.version, s_img.version, LORA_FW_VERSION_LEN);

    for (retries = 0; retries < LORA_OTA_MAX_RETRIES; retries++) {
        // esp_ota_begin() on the node may take a moment: generous timeout.
        err = transact(&start, sizeof(start), node, session, &resp, ack_timeout_ms + 1000);
        if (err == ESP_OK) {
            break;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Node %u didn't answer OTA_START", node);
        return ESP_ERR_TIMEOUT;
    }
    if (resp.hdr.type == LORA_MSG_OTA_NACK) {
        ESP_LOGW(TAG, "Node %u rejected the update (status %u)", node, resp.status);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Session 0x%04X with node %u: %s, %u chunks of %u bytes (~%" PRIu32 " min)",
             session, node, s_img.version, total, chunk,
             (uint32_t)(((uint64_t)total * (sx1276_time_on_air_ms(LORA_MAX_PAYLOAD) +
                         sx1276_time_on_air_ms(sizeof(lora_msg_ota_resp_t)) + 50)) / 60000) + 1);

    /* ---------- DATA ---------- */
    uint8_t pkt[LORA_MAX_PAYLOAD];
    uint16_t cur = 0;
    retries = 0;
    const TickType_t t0 = xTaskGetTickCount();

    while (cur < total) {
        const uint32_t off = (uint32_t)cur * chunk;
        const uint8_t n = (s_img.size - off) < chunk ? (uint8_t)(s_img.size - off) : chunk;

        lora_msg_ota_data_t hdr;
        lora_hdr_fill(&hdr.hdr, LORA_MSG_OTA_DATA, LORA_GATEWAY_ID, node, session);
        hdr.chunk = cur;
        memcpy(pkt, &hdr, LORA_OTA_DATA_HDR_SIZE);
        err = esp_partition_read(s_img.part, off, pkt + LORA_OTA_DATA_HDR_SIZE, n);
        if (err != ESP_OK) {
            send_abort(node, session);
            return err;
        }

        err = transact(pkt, LORA_OTA_DATA_HDR_SIZE + n, node, session, &resp, ack_timeout_ms);

        if (err == ESP_OK && resp.hdr.type == LORA_MSG_OTA_ACK && resp.next_chunk == cur + 1) {
            cur++;
            retries = 0;
            if ((cur % 200) == 0 || cur == total) {
                uint32_t secs = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS / 1000;
                ESP_LOGI(TAG, "Sent %u/%u chunks (%u%%), %" PRIu32 " s", cur, total,
                         (unsigned)(cur * 100U / total), secs);
            }
            continue;
        }

        if (++retries > LORA_OTA_MAX_RETRIES) {
            ESP_LOGE(TAG, "Too many retries on chunk %u, aborting session", cur);
            send_abort(node, session);
            return ESP_ERR_TIMEOUT;
        }
        if (err != ESP_OK) {
            continue;   // no answer: resend the same chunk
        }
        if (resp.hdr.type == LORA_MSG_OTA_NACK) {
            switch (resp.status) {
            case LORA_OTA_ST_CRC_ERROR:
            case LORA_OTA_ST_BAD_SEQ:
            case LORA_OTA_ST_BAD_PACKET:
                // The node says where to resume from.
                if (resp.next_chunk < total) {
                    cur = resp.next_chunk;
                }
                break;
            default:
                // NO_SESSION, FLASH_ERROR...: the node dropped the session.
                ESP_LOGE(TAG, "NACK (status %u) on chunk %u, session over", resp.status, cur);
                return ESP_ERR_TIMEOUT;
            }
        }
        // ACK with an unexpected next_chunk: resend the same chunk.
    }

    /* ---------- END ---------- */
    lora_hdr_t end;
    lora_hdr_fill(&end, LORA_MSG_OTA_END, LORA_GATEWAY_ID, node, session);
    for (retries = 0; retries < LORA_OTA_MAX_RETRIES; retries++) {
        err = transact(&end, sizeof(end), node, session, &resp, LORA_OTA_END_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
    }
    if (err != ESP_OK) {
        // If the node applied the image and rebooted, only the ACK got lost:
        // its next telemetry will report the new version.
        ESP_LOGW(TAG, "No answer to OTA_END; node %u may have applied the image anyway", node);
        return ESP_ERR_TIMEOUT;
    }
    if (resp.hdr.type == LORA_MSG_OTA_NACK) {
        ESP_LOGE(TAG, "Node %u rejected the image at END (status %u)", node, resp.status);
        return ESP_ERR_INVALID_CRC;
    }

    ESP_LOGI(TAG, "Node %u updated to %s, rebooting", node, s_img.version);
    return ESP_OK;
}

esp_err_t ota_lora_gw_serve(uint8_t node_id, const char *requested_version)
{
    if (s_hold) {
        ESP_LOGW(TAG, "Gateway about to reboot, ignoring OTA request from node %u", node_id);
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_img.valid || strncmp(requested_version, s_img.version, LORA_FW_VERSION_LEN) != 0) {
        ESP_LOGW(TAG, "Node %u asked for '%.32s' but the stored image is '%s'",
                 node_id, requested_version, s_img.valid ? s_img.version : "(none)");
        return ESP_ERR_NOT_FOUND;
    }
    // A download (remote_image_fetch) may be rewriting the partition.
    if (!ota_lora_gw_lock(0)) {
        ESP_LOGW(TAG, "Remote image is being updated, ignoring request from node %u", node_id);
        return ESP_ERR_INVALID_STATE;
    }
    s_busy = true;
    esp_err_t err = run_session(node_id);
    s_busy = false;
    ota_lora_gw_unlock();
    return err;
}
