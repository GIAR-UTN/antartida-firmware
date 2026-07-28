/**
 * @file bme280_task.c
 * @brief Lectura periódica del BME280 por I2C.
 */

#include "bme280.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "telemetry.h"

static const char *TAG = "BME280";

#define I2C_MASTER_TIMEOUT_MS 1000
#define BME280_READ_INTERVAL_MS 5 * 1000


static int32_t s_t_fine = 0;
static bool s_is_bme280 = true;
static bool s_i2c_scanned = false;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;
static uint8_t s_i2c_addr = 0;
static bme280_calib_t cal = {0};


esp_err_t i2c_master_init(void)
{
    if (s_i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BME280_I2C_PORT,
        .sda_io_num = BME280_I2C_SDA_GPIO,
        .scl_io_num = BME280_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags.enable_internal_pullup = 0,
    };

    return i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
}

static esp_err_t bme280_select_device(uint8_t addr, i2c_master_dev_handle_t *out_dev)
{
    if (s_i2c_bus == NULL) {
        esp_err_t err = i2c_master_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (s_i2c_dev != NULL && s_i2c_addr == addr) {
        *out_dev = s_i2c_dev;
        return ESP_OK;
    }

    if (s_i2c_dev != NULL) {
        i2c_master_bus_wait_all_done(s_i2c_bus, 100);
        i2c_master_bus_rm_device(s_i2c_dev);
        s_i2c_dev = NULL;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BME280_I2C_FREQ_HZ,
        .scl_wait_us = 0,
        .flags.disable_ack_check = 0,
    };
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_i2c_dev);
    if (err != ESP_OK) {
        return err;
    }
    s_i2c_addr = addr;
    *out_dev = s_i2c_dev;
    return ESP_OK;
}

static esp_err_t bme280_i2c_transmit_receive(uint8_t addr, const uint8_t *tx, size_t tx_len,
                                             uint8_t *rx, size_t rx_len)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = bme280_select_device(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, I2C_MASTER_TIMEOUT_MS);
    if ((err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) && s_i2c_bus != NULL) {
        i2c_master_bus_reset(s_i2c_bus);
        err = i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, I2C_MASTER_TIMEOUT_MS);
    }
    return err;
}

static esp_err_t bme280_i2c_transmit(uint8_t addr, const uint8_t *tx, size_t tx_len)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = bme280_select_device(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, tx, tx_len, I2C_MASTER_TIMEOUT_MS);
    if ((err == ESP_ERR_INVALID_STATE || err == ESP_ERR_TIMEOUT) && s_i2c_bus != NULL) {
        i2c_master_bus_reset(s_i2c_bus);
        err = i2c_master_transmit(dev, tx, tx_len, I2C_MASTER_TIMEOUT_MS);
    }
    return err;
}

static esp_err_t bme280_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, size_t len)
{
    return bme280_i2c_transmit_receive(addr, &reg, 1, data, len);
}

static esp_err_t bme280_write_reg(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return bme280_i2c_transmit(addr, buf, sizeof(buf));
}

static esp_err_t bme280_read_calibration(uint8_t addr, bme280_calib_t *cal)
{
    uint8_t buf1[26];
    uint8_t buf2[7];
    uint8_t h1 = 0;

    esp_err_t err = bme280_read_reg(addr, 0x88, buf1, sizeof(buf1));
    if (err != ESP_OK) {
        return err;
    }
    if (s_is_bme280) {
        err = bme280_read_reg(addr, 0xA1, &h1, 1);
        if (err != ESP_OK) {
            return err;
        }
        err = bme280_read_reg(addr, 0xE1, buf2, sizeof(buf2));
        if (err != ESP_OK) {
            return err;
        }
    }

    cal->dig_T1 = (uint16_t)(buf1[1] << 8 | buf1[0]);
    cal->dig_T2 = (int16_t)(buf1[3] << 8 | buf1[2]);
    cal->dig_T3 = (int16_t)(buf1[5] << 8 | buf1[4]);
    cal->dig_P1 = (uint16_t)(buf1[7] << 8 | buf1[6]);
    cal->dig_P2 = (int16_t)(buf1[9] << 8 | buf1[8]);
    cal->dig_P3 = (int16_t)(buf1[11] << 8 | buf1[10]);
    cal->dig_P4 = (int16_t)(buf1[13] << 8 | buf1[12]);
    cal->dig_P5 = (int16_t)(buf1[15] << 8 | buf1[14]);
    cal->dig_P6 = (int16_t)(buf1[17] << 8 | buf1[16]);
    cal->dig_P7 = (int16_t)(buf1[19] << 8 | buf1[18]);
    cal->dig_P8 = (int16_t)(buf1[21] << 8 | buf1[20]);
    cal->dig_P9 = (int16_t)(buf1[23] << 8 | buf1[22]);
    if (s_is_bme280) {
        cal->dig_H1 = h1;
        cal->dig_H2 = (int16_t)(buf2[1] << 8 | buf2[0]);
        cal->dig_H3 = buf2[2];
        cal->dig_H4 = (int16_t)((buf2[3] << 4) | (buf2[4] & 0x0F));
        cal->dig_H5 = (int16_t)((buf2[5] << 4) | (buf2[4] >> 4));
        cal->dig_H6 = (int8_t)buf2[6];
    } else {
        cal->dig_H1 = 0;
        cal->dig_H2 = 0;
        cal->dig_H3 = 0;
        cal->dig_H4 = 0;
        cal->dig_H5 = 0;
        cal->dig_H6 = 0;
    }

    return ESP_OK;
}

static int32_t bme280_compensate_temp(int32_t adc_T, const bme280_calib_t *cal)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)cal->dig_T1 << 1))) * ((int32_t)cal->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)cal->dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)cal->dig_T1))) >> 12) *
                    ((int32_t)cal->dig_T3)) >> 14;

    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8;
}

static uint32_t bme280_compensate_press(int32_t adc_P, const bme280_calib_t *cal)
{
    int64_t var1 = ((int64_t)s_t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)cal->dig_P6;
    var2 = var2 + ((var1 * (int64_t)cal->dig_P5) << 17);
    var2 = var2 + (((int64_t)cal->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)cal->dig_P3) >> 8) + ((var1 * (int64_t)cal->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)cal->dig_P1)) >> 33;
    if (var1 == 0) {
        return 0;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)cal->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)cal->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)cal->dig_P7) << 4);
    return (uint32_t)p;
}

static uint32_t bme280_compensate_hum(int32_t adc_H, const bme280_calib_t *cal)
{
    if (!s_is_bme280) {
        return 0;
    }
    int32_t v_x1 = s_t_fine - ((int32_t)76800);
    v_x1 = (((((adc_H << 14) - (((int32_t)cal->dig_H4) << 20) -
               (((int32_t)cal->dig_H5) * v_x1)) + ((int32_t)16384)) >> 15) *
            (((((((v_x1 * ((int32_t)cal->dig_H6)) >> 10) *
                (((v_x1 * ((int32_t)cal->dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
               ((int32_t)2097152)) * ((int32_t)cal->dig_H2) + 8192) >> 14));
    v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)cal->dig_H1)) >> 4);
    if (v_x1 < 0) {
        v_x1 = 0;
    }
    if (v_x1 > 419430400) {
        v_x1 = 419430400;
    }
    return (uint32_t)(v_x1 >> 12);
}

static esp_err_t bme280_reset_and_wait(uint8_t addr)
{
    esp_err_t err = bme280_write_reg(addr, BME280_REG_RESET, 0xB6);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // Wait for NVM copy to finish (STATUS[0] == 0)
    for (int i = 0; i < 20; i++) {
        uint8_t status = 0;
        err = bme280_read_reg(addr, BME280_REG_STATUS, &status, 1);
        if (err == ESP_OK && (status & 0x01) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t bme280_init(void)
{
    uint8_t chip_id = 0;
    esp_err_t err = bme280_read_reg(BME280_I2C_ADDR, BME280_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (chip_id == BME280_CHIP_ID) {
        s_is_bme280 = true;
    } else if (chip_id == BMP280_CHIP_ID1 || chip_id == BMP280_CHIP_ID2 || chip_id == BMP280_CHIP_ID3) {
        s_is_bme280 = false;
    } else {
        return ESP_FAIL;
    }

    err = bme280_reset_and_wait(BME280_I2C_ADDR);
    if (err != ESP_OK) {
        return err;
    }

    err = bme280_read_calibration(BME280_I2C_ADDR, &cal);
    if (err != ESP_OK) {
        return err;
    }

    // Temp/Press oversampling x1, sleep mode to configure
    err = bme280_write_reg(BME280_I2C_ADDR, BME280_REG_CTRL_MEAS, 0x24);
    if (err != ESP_OK) {
        return err;
    }
    // Standby 1000 ms, filter off
    err = bme280_write_reg(BME280_I2C_ADDR, BME280_REG_CONFIG, 0xA0);
    if (err != ESP_OK) {
        return err;
    }
    if (s_is_bme280) {
        // Humidity oversampling x1 (must be written before ctrl_meas to latch)
        err = bme280_write_reg(BME280_I2C_ADDR, BME280_REG_CTRL_HUM, 0x01);
        if (err != ESP_OK) {
            return err;
        }
    }
    // Temp/Press oversampling x1, normal mode
    err = bme280_write_reg(BME280_I2C_ADDR, BME280_REG_CTRL_MEAS, 0x27);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

static esp_err_t bme280_read_values(uint8_t addr, const bme280_calib_t *cal,
                                    float *temp_c, float *press_hpa, float *hum_pct)
{
    uint8_t data[8];
    esp_err_t err = bme280_read_reg(addr, BME280_REG_DATA, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    int32_t adc_P = (int32_t)((data[0] << 12) | (data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)((data[3] << 12) | (data[4] << 4) | (data[5] >> 4));
    int32_t adc_H = (int32_t)((data[6] << 8) | data[7]);

    int32_t t = bme280_compensate_temp(adc_T, cal);
    uint32_t p = bme280_compensate_press(adc_P, cal);
    uint32_t h = bme280_compensate_hum(adc_H, cal);

    if (temp_c) {
        *temp_c = (float)t / 100.0f;
    }
    if (press_hpa) {
        float pa = (float)p / 256.0f;
        *press_hpa = pa / 100.0f;
    }
    if (hum_pct) {
        if (s_is_bme280) {
            *hum_pct = (float)h / 1024.0f;
        } else {
            *hum_pct = NAN;
        }
    }

    return ESP_OK;
}




esp_err_t bme280_read(float *temp_c, float *press_hpa, float *hum_pct)
{
    return bme280_read_values(BME280_I2C_ADDR, &cal, temp_c, press_hpa, hum_pct);
}

void bme280_task(void *arg)
{
    (void)arg;
    
    // uint8_t addr = BME280_I2C_ADDR;
    // while (1) {
    //     esp_err_t err = i2c_master_init();
    //     if (err != ESP_OK) {
    //         ESP_LOGE(TAG, "I2C init fallo: 0x%x", err);
    //         vTaskDelay(pdMS_TO_TICKS(2000));
    //         continue;
    //     }

    //     err = bme280_init(addr, &cal);
    //     if (err != ESP_OK) {
    //         ESP_LOGW(TAG, "BME280 no detectado en BME280_I2C_ADDR");
    //         vTaskDelay(pdMS_TO_TICKS(2000));
    //         continue;
    //     }

    //     ESP_LOGI(TAG, "BME280 listo en 0x%02x", addr);
    //     break;
    // }

    float temp_c = 0.0f;
    float press_hpa = 0.0f;
    float hum_pct = 0.0f;

    while (1) 
    {
        esp_err_t err = bme280_read_values(BME280_I2C_ADDR, &cal, &temp_c, &press_hpa, &hum_pct);
        if (err == ESP_OK)
        {
            telemetry_send_bme280(temp_c, hum_pct, press_hpa);
        } else {
            ESP_LOGW(TAG, "BME280 lectura fallo: 0x%x", err);
        }
        vTaskDelay(pdMS_TO_TICKS(BME280_READ_INTERVAL_MS));
    }
}