/**
 * @file bme280_task.h
 * @brief Tarea FreeRTOS para leer BME280 por I2C.
 */

#ifndef BME280_TASK_H
#define BME280_TASK_H

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_types.h"

// I2C pins
#define BME280_I2C_SDA_GPIO GPIO_NUM_21
#define BME280_I2C_SCL_GPIO GPIO_NUM_22

// I2C settings
#define BME280_I2C_PORT     I2C_NUM_0
#define BME280_I2C_FREQ_HZ  100000

// BME280 addresses INVAP MANDA
#define BME280_ADDR_PRIMARY   0x76
#define BME280_ADDR_SECONDARY 0x77

// Dirección preferida (cambiar a 0x76 si tu módulo usa esa)
#define BME280_I2C_ADDR BME280_ADDR_SECONDARY

// BME Regs
#define BME280_REG_CHIP_ID 0xD0
#define BME280_REG_RESET   0xE0
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS  0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG  0xF5
#define BME280_REG_DATA    0xF7

#define BME280_CHIP_ID 0x60
#define BMP280_CHIP_ID1 0x56
#define BMP280_CHIP_ID2 0x57
#define BMP280_CHIP_ID3 0x58

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_t;


void bme280_task(void *arg);
esp_err_t i2c_master_init(void);
esp_err_t bme280_init(void);

#endif // BME280_TASK_H
