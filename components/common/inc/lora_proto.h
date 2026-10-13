/**
 * @file lora_proto.h
 * @brief Wire format of the LoRa link between the remote node and the gateway.
 *
 * Shared by both apps so both ends always agree on the framing. Every frame
 * starts with lora_hdr_t; all multi-byte fields are little-endian (native on
 * ESP32) and all structs are packed. Bump LORA_PROTO_VERSION on any
 * incompatible change -- frames with another version are dropped.
 *
 * Two flows share this link (full description in docs/LORA.md):
 *
 * 1) Telemetry, once per remote wake cycle (remote always initiates, see
 *    proposal sec. 6.5):
 *
 *        remote                                  gateway (always listening)
 *        TELEMETRY (seq, fw version, data)  ->
 *                                           <-   TELEMETRY_ACK (seq, OTA offer)
 *
 *    The remote keeps its receiver on for a short window after TX only to
 *    catch this ACK, then goes back to sleep. The ACK lets the remote know
 *    the reading was delivered (and is what validates a new OTA image).
 *
 * 2) OTA of the remote node, piggybacked on (1): if the ACK offers a version
 *    the remote wants, the remote asks for it and stays awake for a
 *    stop-and-wait session (protocol by Aylen, aylen27s/ota-by-lora):
 *
 *        OTA_REQUEST (version)                ->
 *                                           <-   OTA_START (session, size, CRC32, chunks)
 *        OTA_ACK (next_seq = 0)               ->
 *                                           <-   OTA_DATA (session, seq, bytes)
 *        OTA_ACK (next_seq = seq + 1)         ->
 *        ...
 *                                           <-   OTA_END (session)
 *        OTA_ACK / OTA_NACK (status)          ->   remote validates bytes == size,
 *                                                  CRC32, esp_ota_end(), reboots
 */

#ifndef LORA_PROTO_H
#define LORA_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "sx1276.h"

#define LORA_PROTO_MAGIC            0xA7
#define LORA_PROTO_VERSION          1
#define LORA_GATEWAY_ID             0x00

#define LORA_FW_VERSION_LEN         32      // == sizeof(esp_app_desc_t.version)

// ---------------- Message types ----------------
#define LORA_MSG_TELEMETRY          0x10    // remote -> gateway
#define LORA_MSG_TELEMETRY_ACK      0x11    // gateway -> remote
#define LORA_MSG_OTA_REQUEST        0x20    // remote -> gateway
#define LORA_MSG_OTA_START          0x21    // gateway -> remote
#define LORA_MSG_OTA_DATA           0x22    // gateway -> remote
#define LORA_MSG_OTA_END            0x23    // gateway -> remote
#define LORA_MSG_OTA_ABORT          0x24    // gateway -> remote
#define LORA_MSG_OTA_ACK            0x2A    // remote -> gateway
#define LORA_MSG_OTA_NACK           0x2B    // remote -> gateway

typedef struct __attribute__((packed)) {
    uint8_t  magic;         // LORA_PROTO_MAGIC
    uint8_t  version;       // LORA_PROTO_VERSION
    uint8_t  type;          // LORA_MSG_*
    uint8_t  src;           // sender id (gateway = 0)
    uint8_t  dst;           // receiver id
    uint16_t seq;           // telemetry: packet counter. OTA: session id.
} lora_hdr_t;

// ---------------- Telemetry ----------------
#define LORA_TELEM_FLAG_CAMPBELL_OK     0x01    // campbell_* fields are valid
#define LORA_TELEM_FLAG_BME280_OK       0x02    // bme280_* fields are valid
#define LORA_TELEM_FLAG_PENDING_VERIFY  0x04    // running a new OTA image not yet confirmed

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_TELEMETRY
    char       fw_version[LORA_FW_VERSION_LEN];
    uint32_t   uptime_s;
    uint8_t    flags;                       // LORA_TELEM_FLAG_*
    int64_t    campbell_timestamp_unix;
    float      campbell_air_temp_c;
    float      campbell_rel_humidity_pct;
    float      campbell_wind_speed_ms;
    float      bme280_temp_c;
    float      bme280_press_hpa;
    float      bme280_hum_pct;
} lora_msg_telemetry_t;

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_TELEMETRY_ACK, seq = echoed
    uint8_t    ota_offer;                   // 1: the gateway has an image for this node
    char       ota_version[LORA_FW_VERSION_LEN];
} lora_msg_telemetry_ack_t;

// ---------------- OTA ----------------
typedef enum {
    LORA_OTA_ST_OK = 0,
    LORA_OTA_ST_SAME_VERSION,        // offered version is the one already running
    LORA_OTA_ST_INVALID_VERSION,     // offered version is the last one that rolled back
    LORA_OTA_ST_TOO_BIG,             // image doesn't fit in the OTA partition
    LORA_OTA_ST_BAD_SEQ,             // unexpected seq; next_seq says which one is expected
    LORA_OTA_ST_CRC_ERROR,           // corrupt LoRa packet; next_seq says which one is expected
    LORA_OTA_ST_NO_SESSION,          // DATA/END without START, or from another session
    LORA_OTA_ST_FLASH_ERROR,         // esp_ota_begin/esp_ota_write failed
    LORA_OTA_ST_SIZE_MISMATCH,       // at END: bytes received != announced size
    LORA_OTA_ST_IMAGE_CRC_MISMATCH,  // at END: CRC32 of received bytes != announced
    LORA_OTA_ST_VALIDATE_FAILED,     // esp_ota_end() rejected the image
    LORA_OTA_ST_BAD_PACKET,          // malformed packet
} lora_ota_status_t;

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_OTA_REQUEST
    char       version[LORA_FW_VERSION_LEN];
} lora_msg_ota_request_t;

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_OTA_START, seq = session
    uint32_t   image_size;                  // total bytes of the image
    uint32_t   image_crc32;                 // esp_rom_crc32_le(0, image, image_size)
    uint16_t   total_chunks;
    uint8_t    chunk_size;                  // data bytes per OTA_DATA (last one may be shorter)
    char       version[LORA_FW_VERSION_LEN];
} lora_msg_ota_start_t;

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_OTA_DATA, seq = session
    uint16_t   chunk;                       // 0 .. total_chunks-1
    uint8_t    data[];                      // offset in the image = chunk * chunk_size
} lora_msg_ota_data_t;

typedef struct __attribute__((packed)) {
    lora_hdr_t hdr;                         // type = LORA_MSG_OTA_ACK / _NACK, seq = session
    uint8_t    status;                      // lora_ota_status_t
    uint16_t   next_chunk;                  // next chunk the remote expects
} lora_msg_ota_resp_t;

#define LORA_OTA_DATA_HDR_SIZE      (sizeof(lora_msg_ota_data_t))
#define LORA_OTA_CHUNK_SIZE         (LORA_MAX_PAYLOAD - LORA_OTA_DATA_HDR_SIZE)

// ---------------- Timing ----------------
// Timeouts are derived from the real time-on-air at the configured SF/BW
// (sx1276_time_on_air_ms) so they stay valid if Kconfig changes the SF.
// Margin covers the other side's processing + TX/RX turnaround (~tens of ms,
// more when the remote is writing/erasing a flash sector).
#define LORA_TURNAROUND_MARGIN_MS   300
#define LORA_FLASH_ERASE_MARGIN_MS  700     // esp_ota_write may erase a 4 KB sector
#define LORA_OTA_MAX_RETRIES        8
#define LORA_OTA_END_TIMEOUT_MS     10000   // esp_ota_end() verifies the whole image
#define LORA_OTA_NODE_IDLE_TIMEOUT_MS 30000 // remote aborts the session after this much silence

/** How long to listen for a reply of reply_len bytes, counted from the end
 *  of our own TX (sx1276_send_packet() returns after TxDone). */
static inline uint32_t lora_reply_window_ms(uint8_t reply_len)
{
    return sx1276_time_on_air_ms(reply_len) + LORA_TURNAROUND_MARGIN_MS;
}

/** Fills a header. */
static inline void lora_hdr_fill(lora_hdr_t *h, uint8_t type, uint8_t src, uint8_t dst, uint16_t seq)
{
    h->magic = LORA_PROTO_MAGIC;
    h->version = LORA_PROTO_VERSION;
    h->type = type;
    h->src = src;
    h->dst = dst;
    h->seq = seq;
}

/** True if buf holds at least a header of our protocol/version addressed to me. */
static inline bool lora_hdr_is_for(const uint8_t *buf, uint8_t len, uint8_t me, lora_hdr_t *out)
{
    if (len < sizeof(lora_hdr_t)) {
        return false;
    }
    lora_hdr_t h;
    __builtin_memcpy(&h, buf, sizeof(h));
    if (h.magic != LORA_PROTO_MAGIC || h.version != LORA_PROTO_VERSION || h.dst != me) {
        return false;
    }
    if (out) {
        *out = h;
    }
    return true;
}

_Static_assert(sizeof(lora_msg_telemetry_t) <= LORA_MAX_PAYLOAD, "telemetry frame too big");
_Static_assert(sizeof(lora_msg_ota_start_t) <= LORA_MAX_PAYLOAD, "OTA_START frame too big");
_Static_assert(LORA_OTA_CHUNK_SIZE <= 255, "chunk_size is uint8_t");

#endif // LORA_PROTO_H
