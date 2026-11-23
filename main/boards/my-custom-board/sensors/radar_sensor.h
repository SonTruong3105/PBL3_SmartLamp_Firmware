#ifndef MY_CUSTOM_BOARD_RADAR_SENSOR_H_
#define MY_CUSTOM_BOARD_RADAR_SENSOR_H_

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>

#include <cstddef>
#include <mutex>

class RadarSensor {
public:
    RadarSensor(uart_port_t uart_port, gpio_num_t tx_gpio, gpio_num_t rx_gpio,
                gpio_num_t presence_gpio);

    esp_err_t Initialize();
    bool IsPresent() const;
    bool GetDistance(int* distance_cm) const;

private:
    static void Task(void* arg);
    void ReadTask();
    void Consume(const uint8_t* bytes, size_t length);

    uart_port_t uart_port_;
    gpio_num_t tx_gpio_;
    gpio_num_t rx_gpio_;
    gpio_num_t presence_gpio_;
    mutable std::mutex mutex_;
    uint8_t buffer_[128] = {};
    size_t buffer_size_ = 0;
    int distance_cm_ = 0;
    int64_t last_distance_us_ = 0;
};

#endif  // MY_CUSTOM_BOARD_RADAR_SENSOR_H_
