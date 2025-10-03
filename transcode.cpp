#include "encoder.h"
#include "yuv.h"

#include "cli11.h"

#include <chrono>
#include <fstream>

constexpr float GSUN = 0.07f; // 1 gsun = 0.07 bps per pixel

int main(int argc, char** argv) {
    CLI::App app{"Sonic Video Encoder"};

    int bitrate = 2000000;
    app.add_option("--bitrate", bitrate, "Bitrate for the output video in bps, default is 2000000");

    bool hw = true;
    app.add_option("--hardware", hw, "Use hardware encoding or software encoding, default is true");

    std::string mode = "cbr";
    app.add_option("--mode", mode, "Encoding mode with choice of cbr, vbr, quality, or fast, default is cbr");

    int quality = 100;
    app.add_option("--quality", quality, "Quality for quality-based encoding mode, default is 100");

    int gop = 30;
    app.add_option("--gop", gop, "Group of pictures size, default is 30");

    std::string profile = "main";
    app.add_option("--profile", profile, "H.264 profile with choice of baseline, main, high, or constrained, default is main");

    std::string codec = "h264";
    app.add_option("--codec", codec, "Codec to use for encoding with choice of h264 or h265");

    int width = 1568;
    app.add_option("--width", width, "Width of the input video, 1568 or 2336, default is 1568");

    int height = 720;
    app.add_option("--height", height, "Height of the input video, 720 or 1080, default is 720");

    std::string out = "vid";
    app.add_option("--out", out, "Output file name without extension, default is vid");

    CLI11_PARSE(app, argc, argv);

    std::ifstream file(height == 1080 ? "brainrot1080p.yuv" : "brainrot720p.yuv", std::ios::binary);

    Header inHeader;
    inHeader.width = width;
    inHeader.height = height;
    inHeader.frameRate = Rational(30, 1);
    Header outHeader;
    outHeader.width = width;
    outHeader.height = height;
    outHeader.frameRate = Rational(30, 1);

    std::unique_ptr<FrameParser> parser = std::make_unique<YUVParser>(file, inHeader);

    Encoder encoder(inHeader, outHeader, hw, out, bitrate, mode, quality, gop, profile, codec);
    auto start = std::chrono::high_resolution_clock::now();
    encoder.encode(*parser);
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float, std::milli> duration = end - start;
    printf("Encoding time: %.3f ms\n", duration.count());
}
