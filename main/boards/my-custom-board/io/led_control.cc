#include "led_control.h"

#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>

namespace {
constexpr char kTag[] = "LedControl";
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kTimer = LEDC_TIMER_1;
constexpr ledc_channel_t kWarmChannel = LEDC_CHANNEL_2;
constexpr ledc_channel_t kCoolChannel = LEDC_CHANNEL_3;
constexpr uint32_t kMaxDuty = (1U << 13) - 1;
}  // namespace

LedControl::LedControl(gpio_num_t warm_gpio, gpio_num_t cool_gpio)
    : warm_gpio_(warm_gpio), cool_gpio_(cool_gpio) {
    ledc_timer_config_t timer_config = {
        .speed_mode = kSpeedMode,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = kTimer,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t warm_config = {
        .gpio_num = warm_gpio_,
        .speed_mode = kSpeedMode,
        .channel = kWarmChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&warm_config));

    ledc_channel_config_t cool_config = {
        .gpio_num = cool_gpio_,
        .speed_mode = kSpeedMode,
        .channel = kCoolChannel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = kTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&cool_config));
}

bool LedControl::SetChannels(int warm_percent, int cool_percent) {
    if (warm_percent < 0 || warm_percent > 100 || cool_percent < 0 || cool_percent > 100) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!Apply(kWarmChannel, warm_percent) || !Apply(kCoolChannel, cool_percent)) {
        return false;
    }
    warm_percent_ = warm_percent;
    cool_percent_ = cool_percent;
    return true;
}

int LedControl::warm_percent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return warm_percent_;
}

int LedControl::cool_percent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cool_percent_;
}

bool LedControl::Apply(ledc_channel_t channel, int percent) {
    const uint32_t duty = (kMaxDuty * static_cast<uint32_t>(percent)) / 100;
    esp_err_t err = ledc_set_duty(kSpeedMode, channel, duty);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_set_duty failed on ch %d: %s", channel, esp_err_to_name(err));
        return false;
    }
    err = ledc_update_duty(kSpeedMode, channel);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ledc_update_duty failed on ch %d: %s", channel, esp_err_to_name(err));
        return false;
    }
    return true;
}

void LedControl::RegisterMcpTools() {
    auto& server = McpServer::GetInstance();
    server.AddTool("self.lights.set_warm_cool",
                   "Set warm and cool light brightness from 0 to 100 percent",
                   PropertyList({Property("warm_percent", kPropertyTypeInteger, 0, 0, 100),
                                 Property("cool_percent", kPropertyTypeInteger, 0, 0, 100)}),
                   [this](const PropertyList& properties) -> ReturnValue {
                       if (!SetChannels(properties["warm_percent"].value<int>(),
                                        properties["cool_percent"].value<int>())) {
                           return std::string("Brightness must be between 0 and 100 percent");
                       }
                       return true;
                   });
    server.AddTool("self.lights.get_state", "Get current warm and cool light brightness",
                   PropertyList(), [this](const PropertyList&) -> ReturnValue {
                       std::lock_guard<std::mutex> lock(mutex_);
                       cJSON* result = cJSON_CreateObject();
                       cJSON_AddNumberToObject(result, "warm_percent", warm_percent_);
                       cJSON_AddNumberToObject(result, "cool_percent", cool_percent_);
                       return result;
                   });
}