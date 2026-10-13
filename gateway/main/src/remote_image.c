/**
 * @file remote_image.c
 * @brief See remote_image.h.
 */
#include "remote_image.h"

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "sdkconfig.h"

#include "ota_lora_gw.h"
#include "wdt_sw.h"
#include "wdt_hw.h"

static const char *TAG = "remote_image";

#define DESC_OFFSET     (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))
#define HEAD_LEN        (DESC_OFFSET + sizeof(esp_app_desc_t))
#define SECTOR_SIZE     4096
#define RX_BUF_SIZE     1024
#define HTTP_TIMEOUT_MS 15000
#define FETCH_WDT_MS    (180 * 1000)

static esp_err_t read_full(esp_http_client_handle_t c, uint8_t *buf, int want, int *got)
{
    *got = 0;
    while (*got < want) {
        int r = esp_http_client_read(c, (char *)buf + *got, want - *got);
        if (r < 0) {
            return ESP_FAIL;
        }
        if (r == 0) {
            break;  // EOF
        }
        *got += r;
    }
    return ESP_OK;
}

// Writes len bytes at off, erasing each 4 KB sector right before first use.
static esp_err_t write_at(const esp_partition_t *p, uint32_t off, const uint8_t *data, size_t len,
                          uint32_t *erased_up_to)
{
    while (off + len > *erased_up_to) {
        esp_err_t err = esp_partition_erase_range(p, *erased_up_to, SECTOR_SIZE);
        if (err != ESP_OK) {
            return err;
        }
        *erased_up_to += SECTOR_SIZE;
    }
    return esp_partition_write(p, off, data, len);
}

static esp_err_t download(const esp_partition_t *part)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_SIGGECA_REMOTE_OTA_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(c, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not connect to %s (%s)", cfg.url, esp_err_to_name(err));
        esp_http_client_cleanup(c);
        return err;
    }
    int64_t content_len = esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    if (status != 200) {
        ESP_LOGE(TAG, "HTTP %d from %s", status, cfg.url);
        err = ESP_FAIL;
        goto out;
    }
    if (content_len > 0 && content_len > (int64_t)part->size) {
        ESP_LOGE(TAG, "Remote image (%" PRId64 " bytes) doesn't fit in '%s' (%" PRIu32 " bytes)",
                 content_len, part->label, part->size);
        err = ESP_ERR_INVALID_SIZE;
        goto out;
    }

    // 1) Peek at the app descriptor: skip the download if we already have it.
    static uint8_t buf[RX_BUF_SIZE];
    _Static_assert(HEAD_LEN <= RX_BUF_SIZE, "rx buffer too small for the image header");
    int got = 0;
    if (read_full(c, buf, HEAD_LEN, &got) != ESP_OK || got < (int)HEAD_LEN) {
        ESP_LOGE(TAG, "Short read on image header");
        err = ESP_FAIL;
        goto out;
    }
    esp_app_desc_t desc;
    memcpy(&desc, buf + DESC_OFFSET, sizeof(desc));
    if (desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "%s is not an ESP-IDF app image", cfg.url);
        err = ESP_ERR_INVALID_VERSION;
        goto out;
    }
    char stored[LORA_FW_VERSION_LEN + 1];
    if (ota_lora_gw_image_version(stored) && strncmp(stored, desc.version, sizeof(desc.version)) == 0) {
        ESP_LOGI(TAG, "Remote image %s already stored, nothing to download", stored);
        err = ESP_OK;
        goto out;
    }
    ESP_LOGI(TAG, "Downloading remote image %.32s (stored: %s)", desc.version,
             ota_lora_gw_image_version(stored) ? stored : "none");

    // 2) Stream the whole image into the partition.
    ota_lora_gw_invalidate();
    uint32_t written = 0;
    uint32_t erased_up_to = 0;
    err = write_at(part, 0, buf, got, &erased_up_to);
    written = got;
    while (err == ESP_OK) {
        int r = esp_http_client_read(c, (char *)buf, sizeof(buf));
        if (r < 0) {
            err = ESP_FAIL;
            break;
        }
        if (r == 0) {
            break;
        }
        if (written + r > part->size) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = write_at(part, written, buf, r, &erased_up_to);
        written += r;
        wdt_hw_kick();
    }
    if (err == ESP_OK && !esp_http_client_is_complete_data_received(c)) {
        err = ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Wrote %" PRIu32 " bytes (%s)", written, esp_err_to_name(err));

out:
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

esp_err_t remote_image_fetch(void)
{
    if (strlen(CONFIG_SIGGECA_REMOTE_OTA_URL) == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_partition_t *part = ota_lora_gw_partition();
    if (part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    // gateway_fsm already waited for any LoRa session to finish; this just
    // makes sure one doesn't start while we rewrite the partition.
    if (!ota_lora_gw_lock(5000)) {
        ESP_LOGW(TAG, "LoRa OTA session in progress, skipping remote image download");
        return ESP_ERR_INVALID_STATE;
    }

    rtc_wtd_config(FETCH_WDT_MS);
    esp_err_t err = download(part);
    rtc_wtd_ok();

    // Re-read whatever is in the partition now. If the download broke
    // halfway, esp_image_verify() fails and nothing gets offered.
    esp_err_t reload = ota_lora_gw_reload();
    ota_lora_gw_unlock();

    if (err == ESP_OK && reload != ESP_OK) {
        ESP_LOGE(TAG, "Downloaded remote image failed verification");
        err = reload;
    }
    return err;
}
