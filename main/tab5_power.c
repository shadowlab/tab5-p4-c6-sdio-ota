/* SPDX-License-Identifier: Apache-2.0 */
#include "tab5_power.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_SDA GPIO_NUM_31
#define I2C_SCL GPIO_NUM_32

#define EXPANDER2_ADDR 0x44

#define REG_CHIP_RESET 0x01
#define REG_IO_DIR     0x03 /* 1 = output */
#define REG_OUT_SET    0x05
#define REG_OUT_H_IM   0x07 /* 1 = high impedance */
#define REG_IN_DEF_STA 0x09
#define REG_PULL_EN    0x0B
#define REG_PULL_SEL   0x0D /* 1 = pull-up */
#define REG_INT_MASK   0x11

#define EXP2_WLAN_PWR_EN (1 << 0) /* Power of the ESP32-C6 */

static const char *TAG = "tab5-power";

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(dev, buf, sizeof(buf), 100);
}

esp_err_t tab5_power_on_c6(void)
{
    i2c_master_bus_handle_t bus = NULL;
    i2c_master_dev_handle_t dev = NULL;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK)
        return err;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = EXPANDER2_ADDR,
        .scl_speed_hz = 400000,
    };
    err = i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    if (err != ESP_OK)
        goto out;

    /* Reset the expander, then set it up like the BSP does (bsp_io_expander_pi4ioe_init) with only the C6 supply
     * switched on. The other outputs (USB 5V, charging) are left off, this application doesn't need them. */
    uint8_t cmd = REG_CHIP_RESET, flags;
    err = write_reg(dev, REG_CHIP_RESET, 0xFF);
    if (err != ESP_OK)
        goto out;
    i2c_master_transmit_receive(dev, &cmd, 1, &flags, 1, 100); /* Reading clears the reset flag */
    write_reg(dev, REG_IO_DIR, 0b10111001);
    write_reg(dev, REG_OUT_H_IM, 0b00000110);
    write_reg(dev, REG_PULL_SEL, 0b10111001);
    write_reg(dev, REG_PULL_EN, 0b11111001);
    write_reg(dev, REG_IN_DEF_STA, 0b01000000);
    write_reg(dev, REG_INT_MASK, 0b10111111);
    err = write_reg(dev, REG_OUT_SET, EXP2_WLAN_PWR_EN);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "ESP32-C6 powered");
    vTaskDelay(pdMS_TO_TICKS(100));

out:
    if (dev)
        i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
    return err;
}
