#include "encoder.h"

// Constants
constexpr UINT ENCODE_WIDTH = 1920;
constexpr UINT ENCODE_HEIGHT = 1080;

void runEncode();

int main()
{
    int iterations = 1;
    for (int i = 0; i < iterations; ++i)
    {
        runEncode();
    }
    return 0;
}

void runEncode()
{
    Header inHeader;
    inHeader.width = ENCODE_WIDTH;
    inHeader.height = ENCODE_HEIGHT;
    inHeader.frameRate = Rational(30, 1);
    Header outHeader;
    outHeader.width = ENCODE_WIDTH;
    outHeader.height = ENCODE_HEIGHT;
    outHeader.frameRate = Rational(30, 1);

    std::unique_ptr<FrameParser> parser = std::make_unique<ConstantParser>(300, inHeader);
    
    Encoder encoder(inHeader, outHeader, true, "constant.h264");
    encoder.encode(*parser);
}
