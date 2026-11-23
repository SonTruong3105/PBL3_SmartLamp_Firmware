#include "i2c_sensors.h"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr char kTag[] = "I2cSensors";
constexpr uint8_t kAht20Address = 0x38;
constexpr uint8_t kBh1750Address = 0x23;
constexpr uint32_t kI2cClockHz = 100000;
constexpr int kI2cTimeoutMs = 100;
}  // namespace

I2cSensors::I2cSensors(i2c_master_bus_handle_t bus) : bus_(bus) {}

I2cSensors::~I2cSensors() {
    if (bh1750_ != nullptr) {
        i2c_master_bus_rm_device(bh1750_);
        bh1750_ = nullptr;
    }
    if (aht20_ != nullptr) {
        i2c_master_bus_rm_device(aht20_);
        aht20_ = nullptr;
    }
}

esp_err_t I2cSensors::Initialize() {
    if (bus_ == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (aht20_ == nullptr) {
        esp_err_t err = i2c_master_probe(bus_, kAht20Address, kI2cTimeoutMs);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "AHT20 not responding at 0x%02X: %s", kAht20Address, esp_err_to_name(err));
            return err;
        }

        i2c_device_config_t aht20_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = kAht20Address,
            .scl_speed_hz = kI2cClockHz,
        };
        err = i2c_master_bus_add_device(bus_, &aht20_config, &aht20_);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "Failed to add AHT20 at 0x%02X: %s", kAht20Address, esp_err_to_name(err));
            return err;
        }

        vTaskDelay(pdMS_TO_TICKS(40));
        const uint8_t reset_command = 0xBA;
        err = i2c_master_transmit(aht20_, &reset_command, 1, kI2cTimeoutMs);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "AHT20 reset failed: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(20));

        const uint8_t init_command[] = {0xBE, 0x08, 0x00};
        err = i2c_master_transmit(aht20_, init_command, sizeof(init_command), kI2cTimeoutMs);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "AHT20 calibration command failed: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (bh1750_ == nullptr) {
        esp_err_t err = i2c_master_probe(bus_, kBh1750Address, kI2cTimeoutMs);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "BH1750 not responding at 0x%02X: %s; continuing without illuminance",
                     kBh1750Address, esp_err_to_name(err));
            return ESP_OK;
        }

        i2c_device_config_t bh1750_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = kBh1750Address,
            .scl_speed_hz = kI2cClockHz,
        };
        err = i2c_master_bus_add_device(bus_, &bh1750_config, &bh1750_);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Failed to add BH1750 at 0x%02X: %s; continuing without illuminance",
                     kBh1750Address, esp_err_to_name(err));
            bh1750_ = nullptr;
            return ESP_OK;
        }

        const uint8_t bh1750_mode = 0x10;
        err = i2c_master_transmit(bh1750_, &bh1750_mode, 1, kI2cTimeoutMs);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "BH1750 continuous high-resolution mode failed: %s; continuing without illuminance",
                     esp_err_to_name(err));
            i2c_master_bus_rm_device(bh1750_);
            bh1750_ = nullptr;
        }
    }

    return ESP_OK;
}

esp_err_t I2cSensors::ReadEnvironment(float* temperature_c, float* humidity_percent, float* lux) {
    if (temperature_c == nullptr || humidity_percent == nullptr || lux == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (aht20_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const uint8_t measure_command[] = {0xAC, 0x33, 0x00};
    esp_err_t err = i2c_master_transmit(aht20_, measure_command, sizeof(measure_command), kI2cTimeoutMs);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t aht_data[6] = {};
    for (int attempt = 0; attempt < 3; ++attempt) {
        err = i2c_master_receive(aht20_, aht_data, sizeof(aht_data), kI2cTimeoutMs);
        if (err != ESP_OK) {
            return err;
        }
        if ((aht_data[0] & 0x80) == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if ((aht_data[0] & 0x80) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    if ((aht_data[0] & 0x08) == 0) {
        const uint8_t init_command[] = {0xBE, 0x08, 0x00};
        (void)i2c_master_transmit(aht20_, init_command, sizeof(init_command), kI2cTimeoutMs);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    const uint32_t humidity_raw = (static_cast<uint32_t>(aht_data[1]) << 12) |
                                  (static_cast<uint32_t>(aht_data[2]) << 4) | (aht_data[3] >> 4);
    const uint32_t temperature_raw = (static_cast<uint32_t>(aht_data[3] & 0x0F) << 16) |
                                     (static_cast<uint32_t>(aht_data[4]) << 8) | aht_data[5];
    *humidity_percent = static_cast<float>(humidity_raw) * 100.0f / 1048576.0f;
    *temperature_c = static_cast<float>(temperature_raw) * 200.0f / 1048576.0f - 50.0f;

    *lux = -1.0f;
    if (bh1750_ == nullptr) {
        return ESP_OK;
    }

    uint8_t bh_data[2] = {};
    err = i2c_master_receive(bh1750_, bh_data, sizeof(bh_data), kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "BH1750 read failed: %s", esp_err_to_name(err));
        return ESP_OK;
    }
    const uint16_t illuminance_raw = (static_cast<uint16_t>(bh_data[0]) << 8) | bh_data[1];
    *lux = static_cast<float>(illuminance_raw) / 1.2f;
    return ESP_OK;
}