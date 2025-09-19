#include "encoder.h"
#include "yuv.h"

#include "cli11.h"

#include <fstream>

constexpr float GSUN = 0.07f; // 1 gsun = 0.07 bps per pixel

int main(int argc, char** argv) {
    CLI::App app{"Sonic Video Encoder"};

    int bitrate = 2000000;
    app.add_option("--bitrate", bitrate, "Bitrate for the output video in bps, default is 2000000");

    CLI11_PARSE(app, argc, argv);

    std::ifstream file("sonic720p.yuv", std::ios::binary);

    Header inHeader;
    inHeader.width = 1568;
    inHeader.height = 720;
    inHeader.frameRate = Rational(30, 1);
    Header outHeader;
    outHeader.width = 1568;
    outHeader.height = 720;
    outHeader.frameRate = Rational(30, 1);

    std::unique_ptr<FrameParser> parser = std::make_unique<YUVParser>(file, inHeader);

    Encoder encoder(inHeader, outHeader, true, "vid.h264", bitrate);
    encoder.encode(*parser);
}
