#pragma once

#include "audio/codecs/no_audio_codec.h"
#include <mutex>
#include <vector>

class MyBoardAudioCodec : public NoAudioCodecSimplex {
public:
    MyBoardAudioCodec();
    virtual ~MyBoardAudioCodec() = default;

protected:
    virtual int Read(int16_t* dest, int samples) override;

private:
    std::mutex data_if_mutex_;
    std::vector<int32_t> read_buffer_;
};