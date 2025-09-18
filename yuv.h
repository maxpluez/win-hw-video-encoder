#pragma once

#include "frames.h"

#include <fstream>

class YuvParser : public FrameParser {
    std::istream& in;
    int fn = 0;
    Header header;
public:
    YuvParser(std::istream& input, Header header) : in(input), header(std::move(header)) {}

    std::unique_ptr<Frame> readFrame() override {
        std::vector<uint8_t> dst(header.frameSize());
        if (readFully(in, dst)) {
            auto frame = std::make_unique<Frame>(fn++, std::move(dst));
            frame->timescale = header.timeScale;
            frame->pts = frame->fn * header.frameRate.den;
            frame->duration = int(header.frameRate.den);
            return frame;
        }
        return nullptr;
    }
};
