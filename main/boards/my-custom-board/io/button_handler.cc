#include "button_handler.h"

#include <utility>

ButtonHandler::ButtonHandler(gpio_num_t up_gpio, gpio_num_t down_gpio, gpio_num_t toggle_gpio,
                             std::function<void()> on_up, std::function<void()> on_down,
                             std::function<void()> on_toggle)
    : up_button_(up_gpio), down_button_(down_gpio), toggle_button_(toggle_gpio) {
    up_button_.OnClick(std::move(on_up));
    down_button_.OnClick(std::move(on_down));
    toggle_button_.OnClick(std::move(on_toggle));
}
