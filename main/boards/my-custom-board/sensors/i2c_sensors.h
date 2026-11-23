#ifndef MY_CUSTOM_BOARD_I2C_SENSORS_H_
#define MY_CUSTOM_BOARD_I2C_SENSORS_H_

#include <driver/i2c_master.h>
#include <esp_err.h>

#include <mutex>

class I2cSensors {
public:
    explicit I2cSensors(i2c_master_bus_handle_t bus);
    ~I2cSensors();

    esp_err_t Initialize();
    esp_err_t ReadEnvironment(float* temperature_c, float* humidity_percent, float* lux);

private:
    i2c_master_bus_handle_t bus_;
    i2c_master_dev_handle_t aht20_ = nullptr;
    i2c_master_dev_handle_t bh1750_ = nullptr;
    std::mutex mutex_;
};

#endif  // MY_CUSTOM_BOARD_I2C_SENSORS_H_
