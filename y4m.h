#pragma once

#include "frames.h"

#include <iostream>
#include <cstring>

struct Y4MHeader {
    Header header;
    std::string comment;
    int64_t timeScale = -1;

    static Colorspace parseColorspace(const std::string& s) {
        if (s == "C420jpeg") return Colorspace::C420jpeg;
        if (s == "C420paldv") return Colorspace::C420paldv;
        if (s == "C420mpeg2") return Colorspace::C420mpeg2;
        if (s == "C420") return Colorspace::C420;
        if (s == "C422") return Colorspace::C422;
        if (s == "C444") return Colorspace::C444;
        throw std::invalid_argument("Unknown colorspace: " + s);
    }

    static Y4MHeader fromString(const std::string& line) {
        if (line.substr(0, 10) != "YUV4MPEG2 ")
            throw std::runtime_error("Wrong yuv4mpeg header.");
        Y4MHeader header;
        std::istringstream iss(line.substr(10));
        std::string s;
        while (iss >> s) {
            char key = s[0];
            std::string value = s.substr(1);
            switch (key) {
                case 'W': header.header.width = std::stoi(value); break;
                case 'H': header.header.height = std::stoi(value); break;
                case 'F': header.header.frameRate = Rational::parse(value); header.timeScale = header.header.frameRate.num; break;
                case 'I': header.header.interlacing = interlacingFromChar(s[1]); break;
                case 'A': header.header.aspectRatio = Rational::parse(value); break;
                case 'C': header.header.colorspace = parseColorspace(s); break;
                case 'X': header.comment = value; break;
            }
        }
        return header;
    }
};

class Y4MParser : public FrameParser {
    std::istream& in;
    Y4MHeader header;
    int fn = 0;
    std::vector<uint8_t> frameheader;
    const std::string expectedHeader = "FRAME\n";
public:
    Y4MParser(std::istream& input) : in(input), frameheader(6) {}

    const Y4MHeader& getHeader() {
        if (header.header.width == 0) readHeader();
        return header;
    }

    std::unique_ptr<Frame> readFrame() override {
        if (header.header.width == 0) readHeader();
        if (!readFrameHeader()) return nullptr;
        std::vector<uint8_t> dst(header.header.frameSize());
        if (readFully(in, dst)) {
            auto frame = std::make_unique<Frame>(fn++, std::move(dst));
            frame->timescale = header.timeScale;
            frame->pts = frame->fn * header.header.frameRate.den;
            frame->duration = int(header.header.frameRate.den);
            return frame;
        }
        return nullptr;
    }

    bool readFrameHeader() {
        in.read(reinterpret_cast<char*>(frameheader.data()), 6);
        if (in.gcount() != 6) return false;
        return std::memcmp(frameheader.data(), expectedHeader.data(), 6) == 0;
    }

    void readHeader() {
        std::string line = readLine(in);
        header = Y4MHeader::fromString(line);
    }
};
