#pragma once
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "driver/gpio.h"
#include "hal/spi_types.h"
#include "esp_err.h"
#include <stdint.h>

#define BOARD_SPI_HOST SPI2_HOST

#define DEFINE_RESULT(T, E, NAME) \
typedef struct {                  \
    bool success;                 \
    union {                       \
        T ok;                     \
        E err;                    \
    };                            \
} NAME

DEFINE_RESULT(uint8_t, esp_err_t, I2cReadResult);

void init_board(void);

void i2c_attach_device(i2c_device_config_t cfg, i2c_master_dev_handle_t *handle);
esp_err_t i2c_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
I2cReadResult i2c_read_reg(i2c_master_dev_handle_t dev, uint8_t reg);
esp_err_t i2c_set_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask);
esp_err_t i2c_clear_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask);
esp_err_t i2c_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t val);
