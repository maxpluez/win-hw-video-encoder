#include "encoder.h"
#include "yuv.h"

#include <fstream>

int main() {
    std::ifstream file("sonic720p.yuv", std::ios::binary);

    Header inHeader;
    inHeader.width = 1568;
    inHeader.height = 720;
    inHeader.frameRate = Rational(30, 1);
    Header outHeader;
    outHeader.width = 1568;
    outHeader.height = 720;
    outHeader.frameRate = Rational(30, 1);

    std::unique_ptr<FrameParser> parser = std::make_unique<YuvParser>(file, inHeader);
    
    Encoder encoder(inHeader, outHeader, true);
    encoder.encode(*parser);
}
