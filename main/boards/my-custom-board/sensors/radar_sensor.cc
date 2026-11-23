#include "radar_sensor.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {
constexpr char kTag[] = "RadarSensor";
constexpr uint32_t kBaudRate = 115200;
constexpr uint8_t kHeader[] = {0xF4, 0xF3, 0xF2, 0xF1};
constexpr uint8_t kFooter[] = {0xF8, 0xF7, 0xF6, 0xF5};
constexpr size_t kMaxPayloadSize = 64;
constexpr int64_t kDistanceFreshnessUs = 5000000;
}  // namespace

RadarSensor::RadarSensor(uart_port_t uart_port, gpio_num_t tx_gpio, gpio_num_t rx_gpio,
                         gpio_num_t presence_gpio)
    : uart_port_(uart_port), tx_gpio_(tx_gpio), rx_gpio_(rx_gpio), presence_gpio_(presence_gpio) {}

esp_err_t RadarSensor::Initialize() {
    gpio_config_t gpio_conf = {
        .pin_bit_mask = 1ULL << presence_gpio_,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio_conf);
    if (err != ESP_OK) {
        return err;
    }

    uart_config_t uart_config = {
        .baud_rate = kBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    err = uart_driver_install(uart_port_, 1024, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(uart_port_, &uart_config);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_set_pin(uart_port_, tx_gpio_, rx_gpio_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(Task, "radar_uart", 3072, this, 4, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool RadarSensor::IsPresent() const { return gpio_get_level(presence_gpio_) != 0; }

bool RadarSensor::GetDistance(int* distance_cm) const {
    if (distance_cm == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_distance_us_ == 0 || esp_timer_get_time() - last_distance_us_ > kDistanceFreshnessUs) {
        return false;
    }
    *distance_cm = distance_cm_;
    return true;
}

void RadarSensor::Task(void* arg) { static_cast<RadarSensor*>(arg)->ReadTask(); }

void RadarSensor::ReadTask() {
    uint8_t incoming[64];
    while (true) {
        const int bytes_read =
            uart_read_bytes(uart_port_, incoming, sizeof(incoming), pdMS_TO_TICKS(100));
        if (bytes_read > 0) {
            Consume(incoming, static_cast<size_t>(bytes_read));
        }
    }
}

void RadarSensor::Consume(const uint8_t* bytes, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (buffer_size_ == sizeof(buffer_)) {
            std::memmove(buffer_, buffer_ + 1, --buffer_size_);
        }
        buffer_[buffer_size_++] = bytes[i];
    }

    while (buffer_size_ >= sizeof(kHeader)) {
        const auto* header =
            std::search(buffer_, buffer_ + buffer_size_, std::begin(kHeader), std::end(kHeader));
        if (header != buffer_) {
            if (header == buffer_ + buffer_size_) {
                const size_t keep = std::min(buffer_size_, sizeof(kHeader) - 1);
                std::memmove(buffer_, buffer_ + buffer_size_ - keep, keep);
                buffer_size_ = keep;
                return;
            }
            const size_t skipped = static_cast<size_t>(header - buffer_);
            std::memmove(buffer_, header, buffer_size_ - skipped);
            buffer_size_ -= skipped;
        }
        if (buffer_size_ < 6) {
            return;
        }

        const size_t payload_size =
            static_cast<size_t>(buffer_[4]) | (static_cast<size_t>(buffer_[5]) << 8);
        if (payload_size < 3 || payload_size > kMaxPayloadSize) {
            std::memmove(buffer_, buffer_ + 1, --buffer_size_);
            continue;
        }

        const size_t frame_size = sizeof(kHeader) + 2 + payload_size + sizeof(kFooter);
        if (buffer_size_ < frame_size) {
            return;
        }
        if (std::memcmp(buffer_ + frame_size - sizeof(kFooter), kFooter, sizeof(kFooter)) == 0) {
            const int distance = static_cast<int>(buffer_[7]) | (static_cast<int>(buffer_[8]) << 8);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                distance_cm_ = distance;
                last_distance_us_ = esp_timer_get_time();
            }
            ESP_LOGD(kTag, "Distance update: %d cm (presence frame=%u)", distance,
                     static_cast<unsigned>(buffer_[6]));
            std::memmove(buffer_, buffer_ + frame_size, buffer_size_ - frame_size);
            buffer_size_ -= frame_size;
        } else {
            std::memmove(buffer_, buffer_ + 1, --buffer_size_);
        }
    }
}
