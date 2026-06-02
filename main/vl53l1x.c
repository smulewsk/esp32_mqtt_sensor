#include "vl53l1x.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "common.h"

static const char *TAG = "vl53l1x";
static bool s_present = false;

// VL53L1X I2C address
#define VL53L1X_ADDR 0x29

static esp_err_t i2c_master_init_default(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = VL53L1X_I2C_SDA_GPIO,
        .scl_io_num = VL53L1X_I2C_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = VL53L1X_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(VL53L1X_I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(VL53L1X_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t i2c_master_deinit(void)
{
    return i2c_driver_delete(VL53L1X_I2C_PORT);
}

// Helpers for 16-bit register I2C access (VL53L1X)
static esp_err_t vl53l1x_write_reg8(uint16_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_WRITE, true);
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    i2c_master_write(cmd, addr, 2, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(VL53L1X_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t vl53l1x_write_reg16(uint16_t reg, uint16_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_WRITE, true);
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    uint8_t v[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    i2c_master_write(cmd, addr, 2, true);
    i2c_master_write(cmd, v, 2, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(VL53L1X_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t vl53l1x_read_reg8(uint16_t reg, uint8_t *out)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_WRITE, true);
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    i2c_master_write(cmd, addr, 2, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, out, 1, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(VL53L1X_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t vl53l1x_read_reg16(uint16_t reg, uint16_t *out)
{
    uint8_t buf[2] = {0};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_WRITE, true);
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    i2c_master_write(cmd, addr, 2, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL53L1X_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(VL53L1X_I2C_PORT, cmd, pdMS_TO_TICKS(200));
    i2c_cmd_link_delete(cmd);
    if (err == ESP_OK) *out = ((uint16_t)buf[0] << 8) | buf[1];
    return err;
}

bool vl53l1x_init(void)
{
    esp_err_t err = i2c_master_init_default();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C init failed: %d", err);
        s_present = false;
        return false;
    }

    // Probe by reading the model ID (IDENTIFICATION__MODEL_ID = 0x010F)
    uint16_t model = 0;
    if (vl53l1x_read_reg16(0x010F, &model) != ESP_OK) {
        ESP_LOGW(TAG, "VL53L1X not responding at 0x%02X", VL53L1X_ADDR);
        s_present = false;
        i2c_master_deinit();
        return false;
    }
    ESP_LOGI(TAG, "VL53L1X model id: 0x%04X", model);

    // Expect common model id 0xEACC; continue even if different but warn
    if (model != 0xEACC) {
        ESP_LOGW(TAG, "Unexpected VL53L1X model id 0x%04X (continuing)", model);
    }

    // Software reset (toggle) and small delay
    vl53l1x_write_reg8(0x0000, 0x00);
    vTaskDelay(pdMS_TO_TICKS(2));
    vl53l1x_write_reg8(0x0000, 0x01);
    vTaskDelay(pdMS_TO_TICKS(5));

    // Wait for firmware/boot ready: FIRMWARE__SYSTEM_STATUS (0x0085) bit0
    for (int i = 0; i < 100; ++i) {
        uint8_t st = 0;
        if (vl53l1x_read_reg8(0x0085, &st) == ESP_OK && (st & 0x01)) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Apply a minimal long-range configuration to target ~4m maximum range.
    // These settings are a compact, generic subset tuned for longer range
    // (increase VCSEL periods, adjust signal/ambient estimators and DSS).
    vl53l1x_write_reg16(0x0024, 0x0A00); // DSS_CONFIG__TARGET_TOTAL_RATE_MCPS
    vl53l1x_write_reg8(0x0031, 0x02);
    vl53l1x_write_reg8(0x0038, 8);
    vl53l1x_write_reg8(0x0037, 16);
    vl53l1x_write_reg8(0x0016, 0x01);
    vl53l1x_write_reg8(0x001B, 0xFF);
    vl53l1x_write_reg8(0x003F, 0x00);
    vl53l1x_write_reg8(0x0040, 0x02);
    vl53l1x_write_reg16(0x0072, 0x0000);
    vl53l1x_write_reg16(0x0074, 0x0000);
    vl53l1x_write_reg8(0x0024, 0x38);

    // Long-range VCSEL/timing presets
    vl53l1x_write_reg8(0x0060, 0x0F); // RANGE_CONFIG__VCSEL_PERIOD_A
    vl53l1x_write_reg8(0x0063, 0x0D); // RANGE_CONFIG__VCSEL_PERIOD_B
    vl53l1x_write_reg8(0x0069, 0xB8); // RANGE_CONFIG__VALID_PHASE_HIGH
    vl53l1x_write_reg8(0x0078, 0x0F); // SD_CONFIG__WOI_SD0
    vl53l1x_write_reg8(0x0079, 0x0D); // SD_CONFIG__WOI_SD1
    vl53l1x_write_reg8(0x007A, 14);   // SD_CONFIG__INITIAL_PHASE_SD0
    vl53l1x_write_reg8(0x007B, 14);   // SD_CONFIG__INITIAL_PHASE_SD1

    // Increase measurement timing budget (coarse): set larger ROI/timeout where possible
    vl53l1x_write_reg16(0x0050, 0x4000); // coarse long timeout (device dependent)

    // Additional adjustments for transparent cover (glass) compensation:
    // - increase target rate to improve SNR through cover
    // - increase effective pulse/ambient estimator widths
    // - relax consistency checks and raise thresholds
    vl53l1x_write_reg16(0x0024, 0x1400); // DSS_CONFIG__TARGET_TOTAL_RATE_MCPS (increase)
    vl53l1x_write_reg8(0x0038, 12);       // SIGMA_ESTIMATOR__EFFECTIVE_PULSE_WIDTH_NS (increase)
    vl53l1x_write_reg8(0x0037, 24);       // SIGMA_ESTIMATOR__EFFECTIVE_AMBIENT_WIDTH_NS (increase)
    vl53l1x_write_reg8(0x0016, 0x05);     // ALGO__CROSSTALK_COMPENSATION_VALID_HEIGHT_MM (increase)
    vl53l1x_write_reg8(0x0040, 0x04);     // ALGO__CONSISTENCY_CHECK__TOLERANCE (relax)
    vl53l1x_write_reg16(0x0072, 0x00C8); // SYSTEM__THRESH_RATE_HIGH (raise)
    vl53l1x_write_reg16(0x0074, 0x0032); // SYSTEM__THRESH_RATE_LOW (raise)
    vl53l1x_write_reg8(0x0024, 0x4C);     // DSS_CONFIG__APERTURE_ATTENUATION (increase)

    ESP_LOGI(TAG, "VL53L1X long-range + cover-glass settings applied");

    ESP_LOGI(TAG, "VL53L1X initialized for long range (model=0x%04X)", model);
    s_present = true;
    return true;
}

int vl53l1x_read_range_mm(void)
{
    if (!s_present) return -1;
    esp_err_t err;
    // Single-shot: clear interrupt, start single-shot, poll ready, read result
    // Clear interrupt
    err = vl53l1x_write_reg8(0x0086, 0x01); // SYSTEM__INTERRUPT_CLEAR
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear interrupt (err=%d)", err);
        return -1;
    }

    // Start single-shot
    err = vl53l1x_write_reg8(0x0087, 0x10); // SYSTEM__MODE_START = single_shot
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start single-shot (err=%d)", err);
        return -1;
    }

    // Poll RESULT__INTERRUPT_STATUS (0x0088) bit0 for ready
    uint8_t istat = 0;
    int waited = 0;
    while (waited < 1000) { // up to ~1000ms
        if (vl53l1x_read_reg8(0x0088, &istat) == ESP_OK && (istat & 0x01)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if ((istat & 0x01) == 0) {
        ESP_LOGW(TAG, "Timeout waiting for measurement (istat=0x%02X)", istat);
        return -1;
    }

    // Read final cross-talk corrected range (0x0096/0x0097)
    uint16_t distance = 0;
    if (vl53l1x_read_reg16(0x0096, &distance) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read range registers");
        return -1;
    }

    // Clear interrupt
    vl53l1x_write_reg8(0x0086, 0x01);

    ESP_LOGI(TAG, "Measured distance (raw): %d mm", distance);
    return distance > 0 ? (int)distance : -1;
}