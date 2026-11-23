#include "my_board_audio_codec.h"
#include "config.h"
#include <algorithm>
#include <cstdint>
#include <vector>

#define INMP441_SHIFT_BITS 14  // Khuếch đại +12dB so với 16: biên độ chuẩn cho Opus & ASR, không clipping

MyBoardAudioCodec::MyBoardAudioCodec()
    : NoAudioCodecSimplex(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        SPK_BCLK_GPIO, SPK_WS_GPIO, SPK_DOUT_GPIO, I2S_STD_SLOT_LEFT,
        MIC_BCLK_GPIO, MIC_WS_GPIO, MIC_SD_GPIO, I2S_STD_SLOT_LEFT
    ) {
    // 1 mic mono (chân L/R nối GND -> I2S_STD_SLOT_LEFT)
    input_channels_ = 1;
    input_reference_ = false;
}

int MyBoardAudioCodec::Read(int16_t* dest, int samples) {
    if (dest == nullptr || samples <= 0) return 0;

    std::lock_guard<std::mutex> lock(data_if_mutex_);
    if (read_buffer_.size() < static_cast<size_t>(samples)) {
        read_buffer_.resize(samples);
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(
        rx_handle_,
        read_buffer_.data(),
        samples * sizeof(int32_t),
        &bytes_read,
        pdMS_TO_TICKS(100)
    );
    if (err != ESP_OK || bytes_read == 0) return 0;

    int samples_read = bytes_read / sizeof(int32_t);

    // Bộ lọc chặn DC offset (~20Hz ở 16kHz): giữ nguyên dải trầm giọng nói (85Hz+)
    static float prev_x = 0.0f;
    static float prev_y = 0.0f;
    const float alpha = 0.992f;

    for (int i = 0; i < samples_read; ++i) {
        int32_t raw = read_buffer_[i] >> INMP441_SHIFT_BITS;
        float x = static_cast<float>(
            std::clamp(raw, static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX))
        );
        float y = alpha * (prev_y + x - prev_x);
        prev_x = x;
        prev_y = y;

        dest[i] = static_cast<int16_t>(
            std::clamp(static_cast<int32_t>(y), static_cast<int32_t>(INT16_MIN), static_cast<int32_t>(INT16_MAX))
        );
    }

    return samples_read;
}