#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_adc/adc_oneshot.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define MIC_BCLK_GPIO GPIO_NUM_5
#define MIC_WS_GPIO   GPIO_NUM_4
#define MIC_SD_GPIO   GPIO_NUM_6

#define SPK_BCLK_GPIO GPIO_NUM_15
#define SPK_WS_GPIO   GPIO_NUM_16
#define SPK_DOUT_GPIO GPIO_NUM_17
#define SPK_SD_GPIO   GPIO_NUM_18

#define I2C_SDA_GPIO GPIO_NUM_41
#define I2C_SCL_GPIO GPIO_NUM_42

#define LCD_SCLK_GPIO GPIO_NUM_38
#define LCD_MOSI_GPIO GPIO_NUM_39
#define LCD_RST_GPIO  GPIO_NUM_40
#define LCD_DC_GPIO   GPIO_NUM_19
#define LCD_CS_GPIO   GPIO_NUM_20
#define LCD_WIDTH     320
#define LCD_HEIGHT    240
#define LCD_MIRROR_X  true   // Đổi từ false -> true
#define LCD_MIRROR_Y  false  // Đổi từ true -> false
#define LCD_SWAP_XY   true   // Giữ nguyên
#define LCD_OFFSET_X  0
#define LCD_OFFSET_Y  0 

#define DUST_LED_GPIO     GPIO_NUM_9
#define DUST_ADC_CHANNEL  ADC_CHANNEL_0

#define RADAR_TX_GPIO  GPIO_NUM_47
#define RADAR_RX_GPIO  GPIO_NUM_48
#define RADAR_OUT_GPIO GPIO_NUM_14

#define LED_WARM_GPIO GPIO_NUM_10
#define LED_COOL_GPIO GPIO_NUM_11

#define BTN_UP_GPIO     GPIO_NUM_13
#define BTN_DOWN_GPIO   GPIO_NUM_12
#define BTN_TOGGLE_GPIO GPIO_NUM_8

#endif  // _BOARD_CONFIG_H_
