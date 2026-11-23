#ifndef MY_CUSTOM_BOARD_LED_CONTROL_H_
#define MY_CUSTOM_BOARD_LED_CONTROL_H_

#include <driver/gpio.h>
#include <driver/ledc.h>

#include <mutex>

class LedControl {
public:
    LedControl(gpio_num_t warm_gpio, gpio_num_t cool_gpio);
    void RegisterMcpTools();
    bool SetChannels(int warm_percent, int cool_percent);
    int warm_percent() const;
    int cool_percent() const;

private:
    bool Apply(ledc_channel_t channel, int percent);

    gpio_num_t warm_gpio_;
    gpio_num_t cool_gpio_;
    int warm_percent_ = 0;
    int cool_percent_ = 0;
    mutable std::mutex mutex_;
};

#endif  // MY_CUSTOM_BOARD_LED_CONTROL_H_