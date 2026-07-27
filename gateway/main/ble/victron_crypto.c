/**
 * @file victron_crypto.c
 * @brief Implementación de desencriptado AES-128-CTR para protocolo Victron.
 *
 * Replica la lógica de `victron-ble`:
 * - Verifica byte de clave.
 * - Usa AES-CTR con IV little-endian.
 */

#include "victron_crypto.h"

#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "esp_log.h"
#include "mbedtls/aes.h"

static const char *TAG_CRYPTO = "VICTRON_CRYPTO";

#define VICTRON_ADV_PREFIX 0x10
#define VICTRON_CONTAINER_MIN_LEN 7

typedef struct {
    uint8_t prefix;
    uint16_t model_id;
    uint8_t readout_type;
    uint32_t iv;
    const uint8_t *encrypted_data;
    size_t encrypted_len;
} victron_container_t;

/**
 * @brief Convierte un string hex (p.ej. "A1B2...") a bytes.
 *
 * @param hex      String con caracteres hex (longitud par).
 * @param out      Buffer de salida.
 * @param out_len  Longitud esperada en bytes.
 * @return esp_err_t
 */
static esp_err_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_len)
{
    size_t hex_len = strlen(hex);
    if (hex_len != out_len * 2) {
        ESP_LOGE(TAG_CRYPTO, "Longitud de clave hex inválida: %d (esperado %d)",
                 (int)hex_len, (int)(out_len * 2));
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < out_len; i++) {
        char high = hex[2 * i];
        char low  = hex[2 * i + 1];

        if (!isxdigit((int)high) || !isxdigit((int)low)) {
            ESP_LOGE(TAG_CRYPTO, "Carácter no-hex en posición %d", (int)(2 * i));
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t h = (uint8_t)(isdigit((int)high) ? high - '0' : (tolower((int)high) - 'a' + 10));
        uint8_t l = (uint8_t)(isdigit((int)low)  ? low  - '0' : (tolower((int)low)  - 'a' + 10));

        out[i] = (uint8_t)((h << 4) | l);
    }

    return ESP_OK;
}

esp_err_t victron_crypto_init(victron_crypto_ctx_t *ctx, const char *key_hex)
{
    if (ctx == NULL || key_hex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));

    esp_err_t err = hex_to_bytes(key_hex, ctx->key, VICTRON_AES_KEY_SIZE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CRYPTO, "Error convirtiendo Device Key");
        return err;
    }

    ctx->counter = 0;

    ESP_LOGI(TAG_CRYPTO, "Contexto de crypto inicializado correctamente");
    return ESP_OK;
}

static esp_err_t victron_parse_container(
    const uint8_t *payload,
    size_t len,
    victron_container_t *out)
{
    if (!payload || !out || len < VICTRON_CONTAINER_MIN_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    out->prefix = payload[0];
    out->readout_type = payload[1];
    out->model_id = (uint16_t)(payload[2] | ((uint16_t)payload[3] << 8));
    out->iv = (uint32_t)payload[5] |
              ((uint32_t)payload[6] << 8);
    out->encrypted_data = payload + VICTRON_CONTAINER_MIN_LEN;
    out->encrypted_len = len - VICTRON_CONTAINER_MIN_LEN;

    if (out->prefix != VICTRON_ADV_PREFIX) {
        ESP_LOGW(TAG_CRYPTO, "Prefijo Victron inválido: 0x%02x", out->prefix);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (out->encrypted_len < 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

esp_err_t victron_crypto_decrypt(
    victron_crypto_ctx_t *ctx,
    const uint8_t *encrypted,
    size_t encrypted_len,
    uint8_t *decrypted,
    size_t *decrypted_len)
{
    if (!ctx || !encrypted || !decrypted || !decrypted_len) {
        return ESP_ERR_INVALID_ARG;
    }

    victron_container_t container;
    esp_err_t parse_err = victron_parse_container(encrypted, encrypted_len, &container);
    if (parse_err != ESP_OK) {
        return parse_err;
    }

    ctx->last_model_id = container.model_id;
    ctx->last_readout_type = container.readout_type;
    ctx->last_iv = container.iv;

    ESP_LOGI(TAG_CRYPTO, "Header: model=0x%04x readout=0x%02x iv=0x%06x keychk=0x%02x",
             container.model_id, container.readout_type, (unsigned)container.iv,
             container.encrypted_data[0]);

    if (container.encrypted_data[0] != ctx->key[0]) {
        ESP_LOGE(TAG_CRYPTO, "Byte de verificación de clave no coincide (0x%02x != 0x%02x)",
                 container.encrypted_data[0], ctx->key[0]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t cipher_len = container.encrypted_len - 1;
    size_t padded_len = (cipher_len + 15U) & ~15U;
    if (*decrypted_len < padded_len) {
        ESP_LOGE(TAG_CRYPTO, "Buffer de salida demasiado pequeño");
        return ESP_ERR_NO_MEM;
    }

    uint8_t *cipher_buf = (uint8_t *)malloc(padded_len);
    if (cipher_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t pad_len = (uint8_t)(padded_len - cipher_len);
    memcpy(cipher_buf, container.encrypted_data + 1, cipher_len);
    if (pad_len == 0) {
        pad_len = 16;
    }
    memset(cipher_buf + cipher_len, pad_len, padded_len - cipher_len);

    ctx->counter = container.iv;

    uint8_t nonce[16] = {0};
    nonce[0] = (uint8_t)(container.iv & 0xFF);
    nonce[1] = (uint8_t)((container.iv >> 8) & 0xFF);
    nonce[2] = (uint8_t)((container.iv >> 16) & 0xFF);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    int ret = mbedtls_aes_setkey_enc(&aes, ctx->key, 128);
    if (ret != 0) {
        ESP_LOGE(TAG_CRYPTO, "mbedtls_aes_setkey_enc falló: %d", ret);
        mbedtls_aes_free(&aes);
        free(cipher_buf);
        return ESP_FAIL;
    }

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};

    ret = mbedtls_aes_crypt_ctr(&aes,
                                padded_len,
                                &nc_off,
                                nonce,
                                stream_block,
                                cipher_buf,
                                decrypted);

    mbedtls_aes_free(&aes);
    free(cipher_buf);

    if (ret != 0) {
        ESP_LOGE(TAG_CRYPTO, "mbedtls_aes_crypt_ctr falló: %d", ret);
        return ESP_FAIL;
    }

    *decrypted_len = cipher_len;

    return ESP_OK;
}

void victron_crypto_get_last_header(const victron_crypto_ctx_t *ctx,
                                    uint16_t *model_id,
                                    uint8_t *readout_type,
                                    uint32_t *iv)
{
    if (!ctx) {
        return;
    }
    if (model_id) {
        *model_id = ctx->last_model_id;
    }
    if (readout_type) {
        *readout_type = ctx->last_readout_type;
    }
    if (iv) {
        *iv = ctx->last_iv;
    }
}

