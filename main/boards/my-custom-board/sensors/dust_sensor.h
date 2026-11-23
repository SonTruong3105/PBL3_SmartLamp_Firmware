#ifndef MY_CUSTOM_BOARD_DUST_SENSOR_H_
#define MY_CUSTOM_BOARD_DUST_SENSOR_H_

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_err.h>

#include <mutex>

class DustSensor {
public:
    DustSensor(gpio_num_t led_gpio, adc_channel_t adc_channel);
    ~DustSensor();

    esp_err_t Read(int* raw_value, int* millivolts);

private:
    gpio_num_t led_gpio_;
    adc_channel_t adc_channel_;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t calibration_ = nullptr;
    std::mutex mutex_;
};

#endif  // MY_CUSTOM_BOARD_DUST_SENSOR_H_
