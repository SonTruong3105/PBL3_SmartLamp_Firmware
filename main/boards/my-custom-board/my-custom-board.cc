#include "wifi_board.h"
#include "application.h"
#include "assets/lang_config.h"
#include "config.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "io/button_handler.h"
#include "io/led_control.h"
#include "mcp_server.h"
#include "my_board_audio_codec.h"
#include "sensors/dust_sensor.h"
#include "sensors/i2c_sensors.h"
#include "sensors/radar_sensor.h"
#include "settings.h"
#include "system_info.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "MyCustomBoard"

extern "C" lv_font_t font_vi_13;
extern "C" lv_font_t font_vi_24;

#define FONT_12 (&font_vi_13)
#define FONT_24 (&font_vi_24)

typedef struct {
    lv_obj_t* l;
    lv_obj_t* r;
    int32_t   open_h;
} eyes_t;

typedef enum { ROBOT_LISTEN, ROBOT_THINK, ROBOT_SPEAK } robot_state_t;

class MyCustomDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* dashboard_layer_ = nullptr;
    lv_obj_t* robot_layer_ = nullptr;

    lv_obj_t* big_time_ = nullptr;
    lv_obj_t* big_date_ = nullptr;
    lv_obj_t* lamp_info_ = nullptr;
    lv_obj_t* temp_val_ = nullptr;
    lv_obj_t* hum_val_ = nullptr;
    lv_obj_t* pm25_val_ = nullptr;
    lv_obj_t* lux_val_ = nullptr;
    lv_obj_t* top_mini_time_ = nullptr;
    lv_obj_t* top_mini_temp_ = nullptr;
    lv_obj_t* wifi_label_ = nullptr;

    lv_obj_t* robot_state_lbl_ = nullptr;
    lv_obj_t* robot_chat_lbl_ = nullptr;
    eyes_t mini_eyes_  = { .l = nullptr, .r = nullptr, .open_h = 11 };
    eyes_t robot_eyes_ = { .l = nullptr, .r = nullptr, .open_h = 56 };
    bool is_ai_ = false;
    int last_state_ = -1;

    lv_style_t st_panel, st_eye, st_glow;

    lv_obj_t* create_box(lv_obj_t* parent, int32_t w, int32_t h) {
        lv_obj_t* o = lv_obj_create(parent);
        lv_obj_remove_style_all(o);
        lv_obj_set_size(o, w, h);
        lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        return o;
    }

    void set_bg(lv_obj_t* o, uint32_t hex) {
        lv_obj_set_style_bg_color(o, lv_color_hex(hex), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }

    void set_flex(lv_obj_t* o, lv_flex_flow_t flow, lv_flex_align_t main_a, lv_flex_align_t cross_a) {
        lv_obj_set_flex_flow(o, flow);
        lv_obj_set_flex_align(o, main_a, cross_a, cross_a);
    }

    lv_obj_t* create_label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t hex) {
        lv_obj_t* l = lv_label_create(parent);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_font(l, font, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(hex), 0);
        return l;
    }

    void init_styles() {
        lv_style_init(&st_panel);
        lv_style_set_bg_color(&st_panel, lv_color_hex(0x161e29));
        lv_style_set_bg_opa(&st_panel, LV_OPA_COVER);
        lv_style_set_border_width(&st_panel, 1);
        lv_style_set_border_color(&st_panel, lv_color_hex(0x232f3e));
        lv_style_set_radius(&st_panel, 8);

        lv_style_init(&st_eye);
        lv_style_set_bg_color(&st_eye, lv_color_hex(0x38bdf8));
        lv_style_set_bg_opa(&st_eye, LV_OPA_COVER);
        lv_style_set_radius(&st_eye, 20);

        lv_style_init(&st_glow);
        lv_style_set_shadow_width(&st_glow, 12);
        lv_style_set_shadow_color(&st_glow, lv_color_hex(0x0284c7));
        lv_style_set_shadow_opa(&st_glow, LV_OPA_60);
    }

    static void blink_cb(void* var, int32_t v) {
        eyes_t* e = (eyes_t*)var;
        if (e->l && e->r) {
            lv_obj_set_height(e->l, v);
            lv_obj_set_height(e->r, v);
        }
    }

    static void look_cb(void* var, int32_t v) {
        eyes_t* e = (eyes_t*)var;
        if (e->l && e->r) {
            lv_obj_set_style_translate_x(e->l, v, 0);
            lv_obj_set_style_translate_x(e->r, v, 0);
        }
    }

    static void eyes_blink(eyes_t* e) {
        if (!e->l || !e->r) return;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, e);
        lv_anim_set_exec_cb(&a, blink_cb);
        lv_anim_set_values(&a, e->open_h, 3);
        lv_anim_set_duration(&a, 80);
        lv_anim_set_playback_duration(&a, 100);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    void eyes_look_start(eyes_t* e) {
        if (!e->l || !e->r) return;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, e);
        lv_anim_set_exec_cb(&a, look_cb);
        lv_anim_set_values(&a, -12, 12);
        lv_anim_set_duration(&a, 450);
        lv_anim_set_playback_duration(&a, 450);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }

    void eyes_look_stop(eyes_t* e) {
        lv_anim_del(e, look_cb);
        look_cb(e, 0);
    }

    void eyes_set_color(eyes_t* e, uint32_t hex) {
        if (!e->l || !e->r) return;
        lv_obj_set_style_bg_color(e->l, lv_color_hex(hex), 0);
        lv_obj_set_style_bg_color(e->r, lv_color_hex(hex), 0);
        lv_obj_set_style_shadow_color(e->l, lv_color_hex(hex), 0);
        lv_obj_set_style_shadow_color(e->r, lv_color_hex(hex), 0);
    }

    static void blink_timer_cb(lv_timer_t* t) {
        MyCustomDisplay* disp = (MyCustomDisplay*)lv_timer_get_user_data(t);
        if (disp) eyes_blink(disp->is_ai_ ? &disp->robot_eyes_ : &disp->mini_eyes_);
        lv_timer_set_period(t, lv_rand(2500, 5500));
    }

    static void state_sync_timer_cb(lv_timer_t* t) {
        MyCustomDisplay* disp = (MyCustomDisplay*)lv_timer_get_user_data(t);
        if (!disp) return;

        if (disp->top_bar_) lv_obj_add_flag(disp->top_bar_, LV_OBJ_FLAG_HIDDEN);
        if (disp->status_bar_) lv_obj_add_flag(disp->status_bar_, LV_OBJ_FLAG_HIDDEN);
        if (disp->emoji_label_) lv_obj_add_flag(disp->emoji_label_, LV_OBJ_FLAG_HIDDEN);
        if (disp->emoji_image_) lv_obj_add_flag(disp->emoji_image_, LV_OBJ_FLAG_HIDDEN);

        auto& app = Application::GetInstance();
        auto current_state = app.GetDeviceState();
        int state_val = (int)current_state;

        if (disp->wifi_label_) {
            if (current_state == kDeviceStateStarting || current_state == kDeviceStateWifiConfiguring) {
                lv_label_set_text(disp->wifi_label_, LV_SYMBOL_WIFI " WIFI CONFIG");
                lv_obj_set_style_text_color(disp->wifi_label_, lv_color_hex(0xfacc15), 0);
            } else if (current_state == kDeviceStateConnecting) {
                lv_label_set_text(disp->wifi_label_, LV_SYMBOL_WIFI " KET NOI...");
                lv_obj_set_style_text_color(disp->wifi_label_, lv_color_hex(0xf97316), 0);
            } else {
                lv_label_set_text(disp->wifi_label_, LV_SYMBOL_WIFI " ONLINE");
                lv_obj_set_style_text_color(disp->wifi_label_, lv_color_hex(0x34d399), 0);
            }
        }

        if (state_val == disp->last_state_) return;
        disp->last_state_ = state_val;

        if (current_state == kDeviceStateListening) {
            disp->SetMode(true);
            disp->SetRobotState(ROBOT_LISTEN, "Đang nghe bạn nói...");
        } else if (current_state == kDeviceStateSpeaking) {
            disp->SetMode(true);
            disp->SetRobotState(ROBOT_SPEAK, nullptr);
        } else if (current_state == kDeviceStateConnecting) {
            disp->SetMode(true);
            disp->SetRobotState(ROBOT_THINK, "Đang kết nối trợ lý...");
        } else if (current_state == kDeviceStateIdle || current_state == kDeviceStateStarting) {
            disp->SetMode(false);
        }
    }

public:
    using SpiLcdDisplay::SpiLcdDisplay;

    void SetEmotion(const char* emotion) override {}
    void SetStatus(const char* status) override {}

    void SetChatMessage(const char* role, const char* content) override {
        DisplayLockGuard lock(this);
        if (!content || strlen(content) == 0) return;
        if (strcmp(role, "user") == 0) {
            SetMode(true);
            SetRobotState(ROBOT_THINK, content);
        } else if (strcmp(role, "assistant") == 0 || strcmp(role, "system") == 0) {
            SetMode(true);
            SetRobotState(ROBOT_SPEAK, content);
        }
    }

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        DisplayLockGuard lock(this);

        init_styles();

        uint32_t child_cnt = lv_obj_get_child_count(container_);
        for (uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_add_flag(lv_obj_get_child(container_, i), LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_set_style_pad_all(container_, 0, 0);
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(container_, 320, 240);

        dashboard_layer_ = create_box(container_, 320, 240);
        set_bg(dashboard_layer_, 0x0b1017);
        lv_obj_set_style_pad_all(dashboard_layer_, 8, 0);
        lv_obj_set_style_pad_column(dashboard_layer_, 8, 0);
        set_flex(dashboard_layer_, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* left = create_box(dashboard_layer_, 146, 224);
        lv_obj_add_style(left, &st_panel, 0);
        lv_obj_set_style_radius(left, 10, 0);
        lv_obj_set_style_pad_all(left, 10, 0);
        set_flex(left, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START);

        lv_obj_t* top = create_box(left, LV_PCT(100), 24);
        set_flex(top, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
        wifi_label_ = create_label(top, LV_SYMBOL_WIFI " OFFLINE", FONT_12, 0xf97316);

        lv_obj_t* face = create_box(top, 36, 22);
        set_bg(face, 0x0b1017);
        lv_obj_set_style_border_width(face, 1, 0);
        lv_obj_set_style_border_color(face, lv_color_hex(0x38bdf8), 0);
        lv_obj_set_style_radius(face, 6, 0);
        set_flex(face, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER);

        mini_eyes_.l = create_box(face, 7, mini_eyes_.open_h);
        mini_eyes_.r = create_box(face, 7, mini_eyes_.open_h);
        lv_obj_add_style(mini_eyes_.l, &st_eye, 0);
        lv_obj_add_style(mini_eyes_.r, &st_eye, 0);
        lv_obj_set_style_radius(mini_eyes_.l, 3, 0);
        lv_obj_set_style_radius(mini_eyes_.r, 3, 0);

        lv_obj_t* clock_box = create_box(left, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(clock_box, 4, 0);
        set_flex(clock_box, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        big_time_ = create_label(clock_box, "--:--", FONT_24, 0xf8fafc);
        big_date_ = create_label(clock_box, "Đang tải...", FONT_12, 0xcbd5e1);

        lv_obj_t* pill = create_box(left, LV_PCT(100), 34);
        set_bg(pill, 0x1e293b);
        lv_obj_set_style_radius(pill, 6, 0);
        set_flex(pill, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lamp_info_ = create_label(pill, LV_SYMBOL_CHARGE " 0% | 4500K", FONT_12, 0xfacc15);

        lv_obj_t* right = create_box(dashboard_layer_, 150, 224);
        lv_obj_set_style_pad_gap(right, 6, 0);
        set_flex(right, LV_FLEX_FLOW_ROW_WRAP, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        auto make_card = [this](lv_obj_t* parent, const char* title, const char* unit, uint32_t accent, lv_obj_t** val_out) {
            lv_obj_t* card = create_box(parent, 72, 92);
            lv_obj_add_style(card, &st_panel, 0);
            lv_obj_set_style_clip_corner(card, true, 0);
            lv_obj_set_style_pad_top(card, 0, 0);
            lv_obj_set_style_pad_bottom(card, 6, 0);
            set_flex(card, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* bar = create_box(card, LV_PCT(100), 4);
            set_bg(bar, accent);

            create_label(card, title, FONT_12, 0xcbd5e1);
            *val_out = create_label(card, "--", FONT_24, 0xf8fafc);
            create_label(card, unit, FONT_12, accent);
        };

        make_card(right, "Nhiệt độ", "°C", 0xf97316, &temp_val_);
        make_card(right, "Độ ẩm", "%", 0x38bdf8, &hum_val_);
        make_card(right, "Bụi mịn", "µg", 0xf59e0b, &pm25_val_);
        make_card(right, "Ánh sáng", "lx", 0x10b981, &lux_val_);

        lv_obj_t* hint = create_box(right, 150, 28);
        lv_obj_add_style(hint, &st_panel, 0);
        lv_obj_set_style_radius(hint, 6, 0);
        set_flex(hint, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        create_label(hint, LV_SYMBOL_AUDIO " Nói \"Hi ESP\"...", FONT_12, 0x94a3b8);

        robot_layer_ = create_box(container_, 320, 240);
        set_bg(robot_layer_, 0x090d14);
        set_flex(robot_layer_, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* bar = create_box(robot_layer_, 320, 26);
        set_bg(bar, 0x111827);
        lv_obj_set_style_pad_hor(bar, 12, 0);
        set_flex(bar, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
        top_mini_time_ = create_label(bar, "--:--", FONT_12, 0xcbd5e1);
        robot_state_lbl_ = create_label(bar, "ĐANG NGHE...", FONT_12, 0x38bdf8);
        top_mini_temp_ = create_label(bar, "-- °C", FONT_12, 0xf97316);

        lv_obj_t* stage = create_box(robot_layer_, 320, 124);
        lv_obj_set_style_pad_column(stage, 36, 0);
        set_flex(stage, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        robot_eyes_.l = create_box(stage, 54, robot_eyes_.open_h);
        robot_eyes_.r = create_box(stage, 54, robot_eyes_.open_h);
        lv_obj_add_style(robot_eyes_.l, &st_eye, 0);
        lv_obj_add_style(robot_eyes_.r, &st_eye, 0);
        lv_obj_add_style(robot_eyes_.l, &st_glow, 0);
        lv_obj_add_style(robot_eyes_.r, &st_glow, 0);

        lv_obj_t* wrap = create_box(robot_layer_, 320, 90);
        lv_obj_set_style_pad_hor(wrap, 10, 0);
        lv_obj_set_style_pad_bottom(wrap, 8, 0);

        lv_obj_t* sub = create_box(wrap, 300, 82);
        lv_obj_add_style(sub, &st_panel, 0);
        lv_obj_set_style_radius(sub, 10, 0);
        lv_obj_set_style_pad_all(sub, 8, 0);

        robot_chat_lbl_ = create_label(sub, "Xin chào! Mình đang nghe đây.", FONT_12, 0xf8fafc);
        lv_label_set_long_mode(robot_chat_lbl_, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(robot_chat_lbl_, 282);
        lv_obj_set_style_text_line_space(robot_chat_lbl_, 3, 0);

        lv_obj_add_flag(robot_layer_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(dashboard_layer_);
        lv_obj_move_foreground(robot_layer_);

        lv_timer_create(blink_timer_cb, 3000, this);
        lv_timer_create(state_sync_timer_cb, 200, this);
    }

    void SetMode(bool ai_active) {
        DisplayLockGuard lock(this);
        if (is_ai_ == ai_active) return;
        is_ai_ = ai_active;
        lv_obj_t* show = ai_active ? robot_layer_ : dashboard_layer_;
        lv_obj_t* hide = ai_active ? dashboard_layer_ : robot_layer_;
        lv_obj_add_flag(hide, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(show, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(show);
    }

    void SetSensorReadings(const char* time_str, const char* date_str, const char* t, const char* h, const char* pm, const char* lux) {
        DisplayLockGuard lock(this);
        if (big_time_) lv_label_set_text(big_time_, time_str);
        if (top_mini_time_) lv_label_set_text(top_mini_time_, time_str);
        if (big_date_) lv_label_set_text(big_date_, date_str);
        if (temp_val_) lv_label_set_text(temp_val_, t);
        if (hum_val_) lv_label_set_text(hum_val_, h);
        if (pm25_val_) lv_label_set_text(pm25_val_, pm);
        if (lux_val_) lv_label_set_text(lux_val_, lux);

        char temp_buf[64];
        snprintf(temp_buf, sizeof(temp_buf), "%s °C", t);
        if (top_mini_temp_) lv_label_set_text(top_mini_temp_, temp_buf);
    }

    void SetLampState(int pct, int kelvin) {
        DisplayLockGuard lock(this);
        if (lamp_info_) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %d%% | %dK", LV_SYMBOL_CHARGE, pct, kelvin);
            lv_label_set_text(lamp_info_, buf);
        }
    }

    void SetRobotState(robot_state_t state, const char* chat_text = nullptr) {
        DisplayLockGuard lock(this);
        uint32_t color = 0x38bdf8;
        const char* text = "ĐANG NGHE...";
        if (state == ROBOT_THINK) {
            color = 0xfacc15;
            text = "ĐANG XỬ LÝ...";
            eyes_look_start(&robot_eyes_);
        } else if (state == ROBOT_SPEAK) {
            color = 0x34d399;
            text = "ĐANG TRẢ LỜI...";
            eyes_look_stop(&robot_eyes_);
        } else {
            eyes_look_stop(&robot_eyes_);
        }

        if (robot_state_lbl_) {
            lv_label_set_text(robot_state_lbl_, text);
            lv_obj_set_style_text_color(robot_state_lbl_, lv_color_hex(color), 0);
        }
        eyes_set_color(&robot_eyes_, color);
        if (chat_text && robot_chat_lbl_) {
            lv_label_set_text(robot_chat_lbl_, chat_text);
        }
    }
};

class MyCustomBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    MyCustomDisplay* display_ = nullptr;
    std::unique_ptr<I2cSensors> i2c_sensors_;
    std::unique_ptr<DustSensor> dust_sensor_;
    std::unique_ptr<RadarSensor> radar_sensor_;
    std::unique_ptr<LedControl> led_control_;
    std::unique_ptr<ButtonHandler> button_handler_;
    bool radar_initialized_ = false;

    bool lamp_is_on_ = false;
    int last_warm_pct_ = 40;
    int last_cool_pct_ = 40;
    int target_cct_ = 50;

    float cache_temp_ = 0;
    float cache_hum_ = 0;
    float cache_lux_ = -1;
    float cache_pm25_ = 0;
    bool cache_presence_ = false;
    int cache_dist_ = 0;

    int GetCurrentKelvin() const {
        return 3000 + (3000 * std::clamp(target_cct_, 0, 100)) / 100;
    }

    void ApplyLampState(int total_brightness, int cct_percent, bool turn_on) {
        total_brightness = std::clamp(total_brightness, 0, 100);
        target_cct_ = std::clamp(cct_percent, 0, 100);

        if (!turn_on || total_brightness == 0) {
            lamp_is_on_ = false;
            if (led_control_) {
                led_control_->SetChannels(0, 0);
            }
            return;
        }

        int cool = (total_brightness * target_cct_) / 100;
        int warm = total_brightness - cool;
        warm = std::clamp(warm, 0, 100);
        cool = std::clamp(cool, 0, 100);

        last_warm_pct_ = warm;
        last_cool_pct_ = cool;
        lamp_is_on_ = true;
        if (led_control_) {
            led_control_->SetChannels(warm, cool);
        }
    }

    void SyncLampUi() {
        if (!display_) return;
        int pct = lamp_is_on_ ? std::clamp(last_warm_pct_ + last_cool_pct_, 0, 100) : 0;
        display_->SetLampState(pct, GetCurrentKelvin());
    }

    cJSON* BuildLampStateJson() const {
        cJSON* root = cJSON_CreateObject();
        int total = lamp_is_on_ ? std::clamp(last_warm_pct_ + last_cool_pct_, 0, 100) : 0;
        cJSON_AddBoolToObject(root, "on", lamp_is_on_);
        cJSON_AddNumberToObject(root, "brightness", total);
        cJSON_AddNumberToObject(root, "cct", target_cct_);
        cJSON_AddNumberToObject(root, "kelvin", GetCurrentKelvin());
        return root;
    }

    void InitializeI2c() {
        i2c_master_bus_config_t config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_SDA_GPIO,
            .scl_io_num = I2C_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = { .enable_internal_pullup = 1 },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&config, &i2c_bus_));
    }

    void InitializeSpi() {
        spi_bus_config_t config = {};
        config.mosi_io_num = LCD_MOSI_GPIO;
        config.miso_io_num = GPIO_NUM_NC;
        config.sclk_io_num = LCD_SCLK_GPIO;
        config.quadwp_io_num = GPIO_NUM_NC;
        config.quadhd_io_num = GPIO_NUM_NC;
        config.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &config, SPI_DMA_CH_AUTO));
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_GPIO;
        io_config.dc_gpio_num = LCD_DC_GPIO;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = LCD_RST_GPIO;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, LCD_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, LCD_MIRROR_X, LCD_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new MyCustomDisplay(panel_io_, panel_, LCD_WIDTH, LCD_HEIGHT, LCD_OFFSET_X,
                                       LCD_OFFSET_Y, LCD_MIRROR_X, LCD_MIRROR_Y, LCD_SWAP_XY);
    }

    void InitializeSensors() {
        i2c_sensors_ = std::make_unique<I2cSensors>(i2c_bus_);
        if (i2c_sensors_->Initialize() != ESP_OK) {
            ESP_LOGE(TAG, "I2C sensor initialization failed");
        }
        dust_sensor_ = std::make_unique<DustSensor>(DUST_LED_GPIO, DUST_ADC_CHANNEL);
        radar_sensor_ = std::make_unique<RadarSensor>(UART_NUM_1, RADAR_TX_GPIO, RADAR_RX_GPIO, RADAR_OUT_GPIO);
        radar_initialized_ = (radar_sensor_->Initialize() == ESP_OK);
    }

    void UpdateSensorDisplay() {
        char time_line[32] = "--:--";
        char date_line[64] = "Đang tải...";
        char temp_buf[32] = "--.-";
        char hum_buf[32] = "--";
        char pm25_buf[32] = "--";
        char lux_buf[32] = "--";

        const time_t now = time(nullptr);
        struct tm local_time = {};
        if (localtime_r(&now, &local_time) != nullptr && local_time.tm_year >= 2025 - 1900) {
            std::strftime(time_line, sizeof(time_line), "%H:%M", &local_time);
            const char* wd[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
            snprintf(date_line, sizeof(date_line), "%s, %02d/%02d", wd[local_time.tm_wday], local_time.tm_mday, local_time.tm_mon + 1);
        }

        float temperature = 0, humidity = 0, lux = -1;
        if (i2c_sensors_ && i2c_sensors_->ReadEnvironment(&temperature, &humidity, &lux) == ESP_OK) {
            cache_temp_ = temperature;
            cache_hum_ = humidity;
            cache_lux_ = lux;
        }
        std::snprintf(temp_buf, sizeof(temp_buf), "%.1f", cache_temp_);
        std::snprintf(hum_buf, sizeof(hum_buf), "%.0f", cache_hum_);
        if (cache_lux_ >= 0) std::snprintf(lux_buf, sizeof(lux_buf), "%.0f", cache_lux_);

        if (dust_sensor_) {
            int raw_val = 0, mv = -1;
            if (dust_sensor_->Read(&raw_val, &mv) == ESP_OK && mv >= 0) {
                float volts = mv / 1000.0f;
                float pm25 = std::max(0.0f, 0.17f * volts - 0.1f) * 1000.0f;
                cache_pm25_ = (cache_pm25_ * 0.7f) + (pm25 * 0.3f);
            }
        }
        std::snprintf(pm25_buf, sizeof(pm25_buf), "%.1f", cache_pm25_);

        if (radar_sensor_ && radar_initialized_) {
            cache_presence_ = radar_sensor_->IsPresent();
            int dist = 0;
            if (radar_sensor_->GetDistance(&dist)) cache_dist_ = dist;
        }

        if (display_) {
            display_->SetSensorReadings(time_line, date_line, temp_buf, hum_buf, pm25_buf, lux_buf);
            SyncLampUi();
        }
    }

    void PublishSensorTelemetry() {
        auto& app = Application::GetInstance();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateStarting ||
            state == kDeviceStateWifiConfiguring ||
            state == kDeviceStateActivating ||
            state == kDeviceStateUpgrading) {
            return;
        }

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "jsonrpc", "2.0");
        cJSON_AddStringToObject(root, "method", "notifications/sensor_state");

        cJSON* params = cJSON_CreateObject();
        cJSON_AddNumberToObject(params, "temperature", cache_temp_);
        cJSON_AddNumberToObject(params, "humidity", cache_hum_);
        cJSON_AddNumberToObject(params, "pm25", cache_pm25_);
        if (cache_lux_ >= 0) {
            cJSON_AddNumberToObject(params, "light_lux", cache_lux_);
        }
        cJSON_AddBoolToObject(params, "presence", cache_presence_);
        if (cache_dist_ > 0) {
            cJSON_AddNumberToObject(params, "distance_cm", cache_dist_);
        }
        cJSON_AddItemToObject(root, "params", params);

        char* payload = cJSON_PrintUnformatted(root);
        if (payload) {
            app.SendMcpMessage(std::string(payload));
            cJSON_free(payload);
        }
        cJSON_Delete(root);
    }

    static void SensorDisplayTask(void* arg) {
        auto* board = static_cast<MyCustomBoard*>(arg);
        vTaskDelay(pdMS_TO_TICKS(1500));
        while (true) {
            board->UpdateSensorDisplay();
            board->PublishSensorTelemetry();
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    void AdjustVolume(int delta) {
        auto* codec = GetAudioCodec();
        const int volume = std::clamp(codec->output_volume() + delta, 0, 100);
        codec->SetOutputVolume(volume);
    }

    void InitializeButtons() {
        button_handler_ = std::make_unique<ButtonHandler>(
            BTN_UP_GPIO, BTN_DOWN_GPIO, BTN_TOGGLE_GPIO,
            [this]() { Application::GetInstance().Schedule([this]() { AdjustVolume(10); }); },
            [this]() { Application::GetInstance().Schedule([this]() { AdjustVolume(-10); }); },
            [this]() {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    app.Schedule([this]() { EnterWifiConfigMode(); });
                    return;
                }
                app.Schedule([&app]() { app.ToggleChatState(); });
            });
    }

    void RegisterLampTools() {
        auto& server = McpServer::GetInstance();

        server.AddTool("Lamp.TurnOn", "Bật đèn bàn", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            int current_total = last_warm_pct_ + last_cool_pct_;
            if (current_total <= 0) {
                current_total = 80;
            }
            ApplyLampState(current_total, target_cct_, true);
            SyncLampUi();
            if (display_) display_->SetRobotState(ROBOT_SPEAK, "Đã bật đèn cho bạn!");
            return BuildLampStateJson();
        });

        server.AddTool("Lamp.TurnOff", "Tắt đèn bàn", PropertyList(), [this](const PropertyList&) -> ReturnValue {
            ApplyLampState(0, target_cct_, false);
            SyncLampUi();
            if (display_) display_->SetRobotState(ROBOT_SPEAK, "Đã tắt đèn.");
            return BuildLampStateJson();
        });

        server.AddTool("Lamp.SetBrightness", "Đặt độ sáng tổng 0-100",
            PropertyList({ Property("value", kPropertyTypeInteger, 50, 0, 100) }),
            [this](const PropertyList& p) -> ReturnValue {
                int b = p["value"].value<int>();
                ApplyLampState(b, target_cct_, b > 0);
                SyncLampUi();
                if (display_) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "Đã đặt độ sáng %d%%", b);
                    display_->SetRobotState(ROBOT_SPEAK, buf);
                }
                return BuildLampStateJson();
            });

        server.AddTool("Lamp.SetColorTemp", "Đặt nhiệt độ màu 0-100 (0=Ấm, 100=Lạnh)",
            PropertyList({ Property("value", kPropertyTypeInteger, 50, 0, 100) }),
            [this](const PropertyList& p) -> ReturnValue {
                int cct = p["value"].value<int>();
                int current_total = last_warm_pct_ + last_cool_pct_;
                if (current_total <= 0) {
                    current_total = 80;
                }
                ApplyLampState(current_total, cct, true);
                SyncLampUi();
                if (display_) display_->SetRobotState(ROBOT_SPEAK, "Đã đổi nhiệt màu đèn.");
                return BuildLampStateJson();
            });

        server.AddTool("Lamp.GetState", "Lấy trạng thái đèn hiện tại", PropertyList(),
            [this](const PropertyList&) -> ReturnValue { return BuildLampStateJson(); });
    }

    void RegisterSensorTools() {
        auto& server = McpServer::GetInstance();

        server.AddTool("self.get_environment", "Lấy toàn bộ cảm biến",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                cJSON* result = cJSON_CreateObject();
                cJSON_AddNumberToObject(result, "temperature", cache_temp_);
                cJSON_AddNumberToObject(result, "humidity", cache_hum_);
                if (cache_lux_ >= 0) cJSON_AddNumberToObject(result, "lux", cache_lux_);
                cJSON_AddNumberToObject(result, "pm25", cache_pm25_);
                cJSON_AddBoolToObject(result, "presence", cache_presence_);
                if (cache_dist_ > 0) cJSON_AddNumberToObject(result, "distance_cm", cache_dist_);
                return result;
            });

        server.AddTool("Sensor.Read", "Đọc 1 cảm biến cụ thể",
            PropertyList({ Property("sensor", kPropertyTypeString) }),
            [this](const PropertyList& p) -> ReturnValue {
                std::string s = p["sensor"].value<std::string>();
                cJSON* result = cJSON_CreateObject();
                if (s == "temperature") cJSON_AddNumberToObject(result, "value", cache_temp_);
                else if (s == "humidity") cJSON_AddNumberToObject(result, "value", cache_hum_);
                else if (s == "lux" || s == "light") cJSON_AddNumberToObject(result, "value", cache_lux_);
                else if (s == "presence") cJSON_AddBoolToObject(result, "value", cache_presence_);
                else if (s == "pm25" || s == "dust") cJSON_AddNumberToObject(result, "value", cache_pm25_);
                else return std::string("Unknown sensor");
                return result;
            });
    }

    void RegisterScreenTools() {
        auto& server = McpServer::GetInstance();
        server.AddTool("Screen.SetText", "Hiển thị text lên màn hình TFT",
            PropertyList({ Property("text", kPropertyTypeString) }),
            [this](const PropertyList& p) -> ReturnValue {
                if (!display_) return std::string("Display unavailable");
                std::string text = p["text"].value<std::string>();
                Application::GetInstance().Schedule([this, text]() {
                    display_->SetChatMessage("assistant", text.c_str());
                });
                return true;
            });
    }

    void InitializeTools() {
        led_control_ = std::make_unique<LedControl>(LED_WARM_GPIO, LED_COOL_GPIO);
        RegisterLampTools();
        RegisterScreenTools();
        RegisterSensorTools();
    }

public:
    MyCustomBoard() {
        gpio_config_t amplifier_config = {
            .pin_bit_mask = 1ULL << SPK_SD_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&amplifier_config));
        ESP_ERROR_CHECK(gpio_set_level(SPK_SD_GPIO, 1));

        InitializeI2c();
        InitializeSpi();
        InitializeDisplay();
        InitializeSensors();
        InitializeButtons();
        InitializeTools();

        xTaskCreate(SensorDisplayTask, "sensor_display", 8192, this, 1, nullptr);
    }

    AudioCodec* GetAudioCodec() override {
        static MyBoardAudioCodec audio_codec;
        return &audio_codec;
    }

    Display* GetDisplay() override { return display_; }
};

DECLARE_BOARD(MyCustomBoard);