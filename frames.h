#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Rational {
    int64_t num, den;
    Rational(int64_t n = 1, int64_t d = 1) : num(n), den(d) {}
    float toFloat() const { return float(num) / float(den); }
    int toInt() const { return int(num / den); }
    static Rational parse(const std::string& value) {
        auto pos = value.find(':');
        if (pos == std::string::npos) throw std::invalid_argument("Invalid rational");
        return Rational(std::stoll(value.substr(0, pos)), std::stoll(value.substr(pos + 1)));
    }
    static const Rational ONE;
};
const Rational Rational::ONE = Rational(1, 1);

enum class Colorspace {
    C420jpeg, C420paldv, C420mpeg2, C420, C422, C444
};
inline bool is420(Colorspace c) { return c != Colorspace::C444 && c != Colorspace::C422; }
inline bool is444(Colorspace c) { return c == Colorspace::C444; }
inline bool is422(Colorspace c) { return c == Colorspace::C422; }

enum class Interlacing {
    Progressive, TopFieldFirst, BottomFieldFirst, Mixed
};
inline Interlacing interlacingFromChar(char c) {
    switch (c) {
        case 't': return Interlacing::TopFieldFirst;
        case 'b': return Interlacing::BottomFieldFirst;
        case 'm': return Interlacing::Mixed;
        case 'p': default: return Interlacing::Progressive;
    }
}

inline std::string readLine(std::istream& in) {
    std::string line;
    std::getline(in, line);
    return line;
}

inline bool readFully(std::istream& in, std::vector<uint8_t>& buf) {
    size_t toRead = buf.size();
    size_t read = 0;
    while (read < toRead) {
        in.read(reinterpret_cast<char*>(buf.data() + read), toRead - read);
        size_t n = in.gcount();
        if (n == 0) return false;
        read += n;
    }
    return true;
}

struct Header {
    int width = 0, height = 0;
    Rational frameRate = Rational::ONE;
    Interlacing interlacing = Interlacing::Progressive;
    Rational aspectRatio = Rational::ONE;
    Colorspace colorspace = Colorspace::C420;
    int64_t timeScale = -1;

    int frameSize() const {
        if (is420(colorspace)) return width * height * 3 / 2;
        if (is422(colorspace)) return width * height * 2;
        if (is444(colorspace)) return width * height * 3;
        return 0;
    }
};

struct Frame {
    int fn;
    std::vector<uint8_t> yuv;
    int64_t timescale = 0;
    int64_t pts = 0;
    int duration = 0;
    Frame(int fn_, std::vector<uint8_t>&& yuv_) : fn(fn_), yuv(std::move(yuv_)) {}
};

class FrameParser
{
public:
    virtual std::unique_ptr<Frame> readFrame() = 0;
};

class ConstantParser : public FrameParser {
    int fn = 0;
    int frames = 0;
    Header header;
public:
    ConstantParser(int frames, Header header) : frames(frames), header(std::move(header)) {}

    std::unique_ptr<Frame> readFrame() override {
        if (fn >= frames) return nullptr;
        std::vector<uint8_t> dst(header.frameSize());
        memset(dst.data(), 128, dst.size());
        auto frame = std::make_unique<Frame>(fn++, std::move(dst));
        frame->timescale = header.timeScale;
        frame->pts = frame->fn * header.frameRate.den;
        frame->duration = int(header.frameRate.den);
        return frame;
    }
};
