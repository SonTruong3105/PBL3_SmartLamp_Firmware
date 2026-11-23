#include "dust_sensor.h"

#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

namespace {
constexpr char kTag[] = "DustSensor";
constexpr int kLedPulseOnUs = 280;
constexpr int kReadDelayUs = 40;
constexpr int kPeriodUs = 10000;
}  // namespace

DustSensor::DustSensor(gpio_num_t led_gpio, adc_channel_t adc_channel)
    : led_gpio_(led_gpio), adc_channel_(adc_channel) {
    gpio_config_t gpio_conf = {
        .pin_bit_mask = 1ULL << led_gpio_,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    ESP_ERROR_CHECK(gpio_set_level(led_gpio_, 1));

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle_));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &channel_config));

    adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = ADC_UNIT_1,
        .chan = adc_channel_,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    const esp_err_t calibration_err =
        adc_cali_create_scheme_curve_fitting(&calibration_config, &calibration_);
    if (calibration_err != ESP_OK) {
        ESP_LOGW(kTag, "ADC calibration unavailable: %s", esp_err_to_name(calibration_err));
    }
}

DustSensor::~DustSensor() {
    if (calibration_ != nullptr) {
        adc_cali_delete_scheme_curve_fitting(calibration_);
    }
    if (adc_handle_ != nullptr) {
        adc_oneshot_del_unit(adc_handle_);
    }
}

esp_err_t DustSensor::Read(int* raw_value, int* millivolts) {
    if (raw_value == nullptr || millivolts == nullptr || adc_handle_ == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t sample_start_us = 0;
    esp_err_t err = ESP_OK;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        sample_start_us = esp_timer_get_time();
        err = gpio_set_level(led_gpio_, 0);
        if (err != ESP_OK) {
            return err;
        }

        esp_rom_delay_us(kLedPulseOnUs);
        err = adc_oneshot_read(adc_handle_, adc_channel_, raw_value);
        esp_rom_delay_us(kReadDelayUs);
        const esp_err_t led_err = gpio_set_level(led_gpio_, 1);
        if (err != ESP_OK) {
            return err;
        }
        if (led_err != ESP_OK) {
            return led_err;
        }

        if (calibration_ == nullptr) {
            *millivolts = -1;
        } else {
            err = adc_cali_raw_to_voltage(calibration_, *raw_value, millivolts);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    const int64_t elapsed_us = esp_timer_get_time() - sample_start_us;
    if (elapsed_us > 0 && elapsed_us < kPeriodUs) {
        esp_rom_delay_us(static_cast<uint32_t>(kPeriodUs - elapsed_us));
    }

    return ESP_OK;
}