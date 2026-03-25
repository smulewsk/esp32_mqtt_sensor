#include "vl53l0x.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "vl53l0x";
static bool s_present = false;

// VL53L0X I2C address
#define VL53L0X_ADDR 0x29

static esp_err_t i2c_master_init_default(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = VL53L0X_I2C_SDA_GPIO,
        .scl_io_num = VL53L0X_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = VL53L0X_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(VL53L0X_I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(VL53L0X_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

bool vl53l0x_init(void)
{
    esp_err_t err = i2c_master_init_default();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C init failed: %d", err);
        s_present = false;
        return false;
    }

    // simple probe: try writing zero bytes to device address and check ACK
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(VL53L0X_I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "VL53L0X found at 0x%02X", VL53L0X_ADDR);
        s_present = true;
        // Note: full sensor init/config sequence not implemented here.
        return true;
    } else {
        ESP_LOGW(TAG, "VL53L0X not found (err=%d)", err);
        s_present = false;
        return false;
    }
}

int vl53l0x_read_range_mm(void)
{
    if (!s_present) return -1;
    esp_err_t err;

    // Start single-shot ranging: write 0x01 to SYSRANGE_START (reg 0x00)
    uint8_t cmd_reg[2] = {0x00, 0x01};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, cmd_reg, 2, true);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(VL53L0X_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start ranging (err=%d)", err);
        return -1;
    }

    // Wait for measurement to complete. Typical timing is < 100ms for single-shot.
    vTaskDelay(pdMS_TO_TICKS(100));

    // Read two bytes from RESULT_RANGE_SHORT (distance) registers 0x1E (MSB), 0x1F (LSB)
    uint8_t reg = 0x1E;
    uint8_t data[2] = {0};
    err = i2c_master_write_read_device(VL53L0X_I2C_PORT, VL53L0X_ADDR, &reg, 1, data, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read range (err=%d)", err);
        return -1;
    }

    int distance_mm = ((int)data[0] << 8) | data[1];

    // stop ranging (clear SYSRANGE_START)
    uint8_t stop_cmd[2] = {0x00, 0x00};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L0X_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, stop_cmd, 2, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(VL53L0X_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    ESP_LOGI(TAG, "Measured distance: %d mm", distance_mm);
    return distance_mm > 0 ? distance_mm : -1;
}
