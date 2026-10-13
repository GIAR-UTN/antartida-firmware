/**
 * @file ota_lora_node.c
 * @brief See ota_lora_node.h. Based on Aylen's ota_lora_node.c.
 */
#include "ota_lora_node.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_rom_crc.h"
#include "sx1276.h"
#include "lora_proto.h"
#include "lora_tx.h"
#include "wdt_hw.h"

static const char *TAG = "ota_lora_node";

#define OTA_REQUEST_ATTEMPTS 3

typedef struct {
    bool                    active;
    uint16_t                session;
    uint32_t                image_size;
    uint32_t                image_crc32;
    uint16_t                total_chunks;
    uint8_t                 chunk_size;
    uint16_t                next_chunk;
    uint32_t                bytes_written;
    uint32_t                crc32;
    esp_ota_handle_t        handle;
    const esp_partition_t  *partition;
} ota_session_t;

typedef enum {
    STEP_CONTINUE,      // keep receiving
    STEP_DONE,          // image applied, reboot
    STEP_ABORTED,       // session over without an image
} step_t;

static ota_session_t s_ota;

static void send_resp(uint8_t type, uint16_t session, lora_ota_status_t status, uint16_t next_chunk)
{
    lora_msg_ota_resp_t resp;
    lora_hdr_fill(&resp.hdr, type, lora_tx_node_id(), LORA_GATEWAY_ID, session);
    resp.status = (uint8_t)status;
    resp.next_chunk = next_chunk;
    esp_err_t err = sx1276_send_packet((const uint8_t *)&resp, sizeof(resp));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not send response (%s)", esp_err_to_name(err));
    }
}

static inline void send_ack(uint16_t session, uint16_t next_chunk)
{
    send_resp(LORA_MSG_OTA_ACK, session, LORA_OTA_ST_OK, next_chunk);
}

static inline void send_nack(uint16_t session, lora_ota_status_t status, uint16_t next_chunk)
{
    send_resp(LORA_MSG_OTA_NACK, session, status, next_chunk);
}

static void session_abort(const char *reason)
{
    if (!s_ota.active) {
        return;
    }
    ESP_LOGW(TAG, "Session 0x%04X aborted: %s (%" PRIu32 "/%" PRIu32 " bytes)",
             s_ota.session, reason, s_ota.bytes_written, s_ota.image_size);
    esp_ota_abort(s_ota.handle);
    memset(&s_ota, 0, sizeof(s_ota));
}

static lora_ota_status_t check_version(const char *new_version)
{
    const esp_app_desc_t *running = esp_app_get_description();
    if (strncmp(new_version, running->version, sizeof(running->version)) == 0) {
        return LORA_OTA_ST_SAME_VERSION;
    }

    // FIX (Aylen): invalid_app_info was compared even when it couldn't be read.
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_info;
    if (last_invalid != NULL &&
        esp_ota_get_partition_description(last_invalid, &invalid_info) == ESP_OK &&
        strncmp(new_version, invalid_info.version, sizeof(invalid_info.version)) == 0) {
        return LORA_OTA_ST_INVALID_VERSION;
    }
    return LORA_OTA_ST_OK;
}

bool ota_lora_node_wants(const char *version)
{
    if (version == NULL || version[0] == '\0') {
        return false;
    }
    lora_ota_status_t st = check_version(version);
    if (st == LORA_OTA_ST_INVALID_VERSION) {
        ESP_LOGW(TAG, "Offered version %s already rolled back here, ignoring offer", version);
    }
    return st == LORA_OTA_ST_OK;
}

static void handle_start(const uint8_t *buf, uint8_t len)
{
    if (len < sizeof(lora_msg_ota_start_t)) {
        return;
    }
    lora_msg_ota_start_t msg;
    memcpy(&msg, buf, sizeof(msg));
    msg.version[sizeof(msg.version) - 1] = '\0';
    const uint16_t session = msg.hdr.seq;

    if (s_ota.active) {
        if (s_ota.session == session) {
            // START repeated (our ACK got lost): re-confirm without restarting.
            send_ack(s_ota.session, s_ota.next_chunk);
            return;
        }
        session_abort("new session received");
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        ESP_LOGE(TAG, "No OTA partition available (check partitions.csv)");
        send_nack(session, LORA_OTA_ST_FLASH_ERROR, 0);
        return;
    }

    uint32_t expected_chunks = msg.chunk_size ? (msg.image_size + msg.chunk_size - 1) / msg.chunk_size : 0;
    if (msg.image_size == 0 || msg.chunk_size == 0 || msg.chunk_size > LORA_OTA_CHUNK_SIZE ||
        msg.total_chunks != expected_chunks) {
        send_nack(session, LORA_OTA_ST_BAD_PACKET, 0);
        return;
    }
    if (msg.image_size > part->size) {
        send_nack(session, LORA_OTA_ST_TOO_BIG, 0);
        return;
    }

    lora_ota_status_t st = check_version(msg.version);
    if (st != LORA_OTA_ST_OK) {
        ESP_LOGW(TAG, "Update rejected (status %d)", st);
        send_nack(session, st, 0);
        return;
    }

    // FIX (Aylen): esp_ota_begin() used to run on every packet. Once per session.
    // OTA_WITH_SEQUENTIAL_WRITES erases sector by sector while writing instead of
    // blocking for seconds erasing the whole partition up front.
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        send_nack(session, LORA_OTA_ST_FLASH_ERROR, 0);
        return;
    }

    s_ota = (ota_session_t) {
        .active = true,
        .session = session,
        .image_size = msg.image_size,
        .image_crc32 = msg.image_crc32,
        .total_chunks = msg.total_chunks,
        .chunk_size = msg.chunk_size,
        .handle = handle,
        .partition = part,
    };

    ESP_LOGI(TAG, "Session 0x%04X: %s, %" PRIu32 " bytes in %u chunks -> %s",
             session, msg.version, s_ota.image_size, s_ota.total_chunks, part->label);
    send_ack(session, 0);
}

static void handle_data(const uint8_t *buf, uint8_t len)
{
    if (len < LORA_OTA_DATA_HDR_SIZE) {
        return;
    }
    lora_msg_ota_data_t msg;
    memcpy(&msg, buf, LORA_OTA_DATA_HDR_SIZE);
    const uint8_t *data = buf + LORA_OTA_DATA_HDR_SIZE;
    const uint32_t data_len = len - LORA_OTA_DATA_HDR_SIZE;

    if (!s_ota.active || msg.hdr.seq != s_ota.session) {
        send_nack(msg.hdr.seq, LORA_OTA_ST_NO_SESSION, 0);
        return;
    }

    if (msg.chunk + 1 == s_ota.next_chunk) {
        // Duplicate: the gateway missed our ACK. Don't write it again.
        send_ack(s_ota.session, s_ota.next_chunk);
        return;
    }
    if (msg.chunk != s_ota.next_chunk || msg.chunk >= s_ota.total_chunks) {
        ESP_LOGW(TAG, "Unexpected chunk %u, expecting %u", msg.chunk, s_ota.next_chunk);
        send_nack(s_ota.session, LORA_OTA_ST_BAD_SEQ, s_ota.next_chunk);
        return;
    }

    // Byte validation: each chunk must carry exactly what's left for its
    // offset (chunk_size, except for the last one).
    uint32_t remaining = s_ota.image_size - s_ota.bytes_written;
    uint32_t expected_len = remaining < s_ota.chunk_size ? remaining : s_ota.chunk_size;
    if (data_len != expected_len) {
        ESP_LOGW(TAG, "Chunk %u has %" PRIu32 " bytes, expected %" PRIu32, msg.chunk, data_len, expected_len);
        send_nack(s_ota.session, LORA_OTA_ST_BAD_PACKET, s_ota.next_chunk);
        return;
    }

    esp_err_t err = esp_ota_write(s_ota.handle, data, data_len);
    if (err != ESP_OK) {
        // ESP_ERR_OTA_VALIDATE_FAILED on the first chunk = not an ESP-IDF image
        ESP_LOGE(TAG, "esp_ota_write failed (%s)", esp_err_to_name(err));
        uint16_t session = s_ota.session;
        session_abort("write error");
        send_nack(session, LORA_OTA_ST_FLASH_ERROR, 0);
        return;
    }

    s_ota.crc32 = esp_rom_crc32_le(s_ota.crc32, data, data_len);
    s_ota.bytes_written += data_len;
    s_ota.next_chunk++;

    if ((s_ota.next_chunk % 200) == 0 || s_ota.next_chunk == s_ota.total_chunks) {
        ESP_LOGI(TAG, "Received %" PRIu32 "/%" PRIu32 " bytes (%" PRIu32 "%%)",
                 s_ota.bytes_written, s_ota.image_size,
                 (uint32_t)((uint64_t)s_ota.bytes_written * 100 / s_ota.image_size));
    }

    send_ack(s_ota.session, s_ota.next_chunk);
}

static step_t handle_end(uint16_t session)
{
    if (!s_ota.active || session != s_ota.session) {
        send_nack(session, LORA_OTA_ST_NO_SESSION, 0);
        return STEP_CONTINUE;
    }

    // Validate bytes received against the image's total size
    if (s_ota.bytes_written != s_ota.image_size) {
        ESP_LOGE(TAG, "END with %" PRIu32 "/%" PRIu32 " bytes", s_ota.bytes_written, s_ota.image_size);
        send_nack(s_ota.session, LORA_OTA_ST_SIZE_MISMATCH, s_ota.next_chunk);
        return STEP_CONTINUE;
    }
    if (s_ota.crc32 != s_ota.image_crc32) {
        ESP_LOGE(TAG, "Image CRC32 0x%08" PRIx32 " != announced 0x%08" PRIx32, s_ota.crc32, s_ota.image_crc32);
        session_abort("image CRC32 mismatch");
        send_nack(session, LORA_OTA_ST_IMAGE_CRC_MISMATCH, 0);
        return STEP_ABORTED;
    }

    const esp_partition_t *part = s_ota.partition;

    // esp_ota_end() verifies the whole image (headers, checksum, appended
    // SHA-256) and frees the handle even on failure -- no esp_ota_abort after.
    wdt_hw_kick();
    esp_err_t err = esp_ota_end(s_ota.handle);
    memset(&s_ota, 0, sizeof(s_ota));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed (%s)", esp_err_to_name(err));
        send_nack(session, LORA_OTA_ST_VALIDATE_FAILED, 0);
        return STEP_ABORTED;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)", esp_err_to_name(err));
        send_nack(session, LORA_OTA_ST_FLASH_ERROR, 0);
        return STEP_ABORTED;
    }

    ESP_LOGI(TAG, "Image received and verified. Rebooting into it.");
    send_ack(session, 0);
    return STEP_DONE;
}

/*
    After applying the image, keep answering repeated ENDs for a few seconds
    (in case the gateway missed the ACK) before rebooting.
*/
static void __attribute__((noreturn)) linger_and_restart(uint16_t session)
{
    uint8_t buf[LORA_MAX_PAYLOAD];
    uint8_t len;
    const TickType_t until = xTaskGetTickCount() + pdMS_TO_TICKS(3000);

    while ((int32_t)(until - xTaskGetTickCount()) > 0) {
        sx1276_start_receive();
        lora_hdr_t hdr;
        if (sx1276_receive_packet(buf, &len, NULL, NULL, 500) == ESP_OK &&
            lora_hdr_is_for(buf, len, lora_tx_node_id(), &hdr) &&
            hdr.type == LORA_MSG_OTA_END && hdr.seq == session) {
            send_ack(session, 0);
        }
    }
    sx1276_sleep();
    esp_restart();
}

static step_t handle_packet(const uint8_t *buf, uint8_t len)
{
    lora_hdr_t hdr;
    if (!lora_hdr_is_for(buf, len, lora_tx_node_id(), &hdr) || hdr.src != LORA_GATEWAY_ID) {
        return STEP_CONTINUE;
    }

    switch (hdr.type) {
    case LORA_MSG_OTA_START:
        handle_start(buf, len);
        return STEP_CONTINUE;
    case LORA_MSG_OTA_DATA:
        handle_data(buf, len);
        return STEP_CONTINUE;
    case LORA_MSG_OTA_END: {
        step_t step = handle_end(hdr.seq);
        if (step == STEP_DONE) {
            linger_and_restart(hdr.seq);
        }
        return step;
    }
    case LORA_MSG_OTA_ABORT:
        if (s_ota.active && hdr.seq == s_ota.session) {
            session_abort("ABORT from gateway");
            return STEP_ABORTED;
        }
        return STEP_CONTINUE;
    default:
        return STEP_CONTINUE;
    }
}

static esp_err_t send_request(const char *version)
{
    lora_msg_ota_request_t req = { 0 };
    lora_hdr_fill(&req.hdr, LORA_MSG_OTA_REQUEST, lora_tx_node_id(), LORA_GATEWAY_ID, 0);
    strncpy(req.version, version, sizeof(req.version) - 1);
    return sx1276_send_packet((const uint8_t *)&req, sizeof(req));
}

esp_err_t ota_lora_node_run(const char *version)
{
    uint8_t buf[LORA_MAX_PAYLOAD];
    uint8_t len = 0;
    memset(&s_ota, 0, sizeof(s_ota));

    ESP_LOGI(TAG, "Requesting version %s from gateway", version);

    // 1) Ask for the image until a START arrives (or give up).
    const uint32_t start_window_ms = lora_reply_window_ms(sizeof(lora_msg_ota_start_t)) + 1000;
    for (int attempt = 1; attempt <= OTA_REQUEST_ATTEMPTS && !s_ota.active; attempt++) {
        if (send_request(version) != ESP_OK) {
            continue;
        }
        const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(start_window_ms);
        sx1276_start_receive();
        while (!s_ota.active) {
            int32_t left = (int32_t)(deadline - xTaskGetTickCount());
            if (left <= 0) {
                break;
            }
            uint32_t left_ms = (uint32_t)left * portTICK_PERIOD_MS;
            if (sx1276_receive_packet(buf, &len, NULL, NULL, left_ms ? left_ms : 1) == ESP_OK) {
                lora_hdr_t hdr;
                if (lora_hdr_is_for(buf, len, lora_tx_node_id(), &hdr) && hdr.type == LORA_MSG_OTA_START) {
                    handle_start(buf, len);
                    if (!s_ota.active) {
                        return ESP_ERR_INVALID_STATE;   // we NACKed it (version, size...)
                    }
                }
            }
        }
    }
    if (!s_ota.active) {
        ESP_LOGW(TAG, "Gateway didn't start a session");
        return ESP_ERR_TIMEOUT;
    }

    // 2) Receive the image. Every packet resets the idle timer and kicks the
    //    HW watchdog: this can take tens of minutes inside a single FSM state.
    TickType_t last_rx = xTaskGetTickCount();
    while (1) {
        wdt_hw_kick();
        sx1276_start_receive();
        esp_err_t err = sx1276_receive_packet(buf, &len, NULL, NULL, 1000);

        if (err == ESP_ERR_TIMEOUT) {
            if ((xTaskGetTickCount() - last_rx) > pdMS_TO_TICKS(LORA_OTA_NODE_IDLE_TIMEOUT_MS)) {
                session_abort("timeout, no packets from gateway");
                return ESP_ERR_TIMEOUT;
            }
            continue;
        }
        if (err == ESP_ERR_INVALID_CRC) {
            // Corrupt packet: ask for a retransmission now instead of waiting
            // for the gateway's timeout.
            send_nack(s_ota.session, LORA_OTA_ST_CRC_ERROR, s_ota.next_chunk);
            continue;
        }
        if (err != ESP_OK) {
            continue;
        }

        last_rx = xTaskGetTickCount();
        if (handle_packet(buf, len) == STEP_ABORTED) {
            return ESP_FAIL;
        }
        if (!s_ota.active) {
            // A write/CRC error ended the session from our side.
            return ESP_FAIL;
        }
    }
}
