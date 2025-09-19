#pragma once

#include "frames.h"

#include <fstream>

constexpr int kMfTicksPerSec = 10000000;

class YUVParser : public FrameParser {
    std::istream& in;
    int fn = 0;
    Header header;
    int durationTicks = 1;
public:
    // duration in ticks per frame is ticks per sec * sec per frame
    YUVParser(std::istream& input, Header header) : in(input), header(std::move(header)), durationTicks(kMfTicksPerSec * header.frameRate.den / header.frameRate.num) {}

    std::unique_ptr<Frame> readFrame() override {
        std::vector<uint8_t> dst(header.frameSize());
        if (readFully(in, dst)) {
            auto frame = std::make_unique<Frame>(fn++, std::move(dst));
            frame->timescale = header.timeScale;
            frame->pts = frame->fn * durationTicks;
            frame->duration = durationTicks;
            return frame;
        }
        return nullptr;
    }
};
