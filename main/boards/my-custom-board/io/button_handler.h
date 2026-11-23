#ifndef MY_CUSTOM_BOARD_IO_BUTTON_HANDLER_H_
#define MY_CUSTOM_BOARD_IO_BUTTON_HANDLER_H_

#include <driver/gpio.h>
#include <functional>
#include "button.h"

class ButtonHandler {
public:
    ButtonHandler(gpio_num_t up_gpio, gpio_num_t down_gpio, gpio_num_t toggle_gpio,
                  std::function<void()> on_up, std::function<void()> on_down,
                  std::function<void()> on_toggle);
    ~ButtonHandler() = default;

private:
    Button up_button_;
    Button down_button_;
    Button toggle_button_;
};

#endif  // MY_CUSTOM_BOARD_IO_BUTTON_HANDLER_H_