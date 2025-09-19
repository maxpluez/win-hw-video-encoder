#pragma once

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")

// RBX
#include "frames.h"

// std
#include <string>
#include <iostream>
#include <fstream>

// Windows
#include <windows.h>
#include <atlbase.h>

// DirectX
#include <dxgi.h>
#include <d3d11.h>

// Media Foundation
#include <mfapi.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <mftransform.h>
#include <mfobjects.h>

// Error handling
#define CHECK(x) if (!(x)) { printf("%s(%d) %s was false\n", __FILE__, __LINE__, #x); return; }
#define CHECK_HR(x) { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s(%d) %s failed with 0x%x\n", __FILE__, __LINE__, #x, (unsigned int)hr_); return; } }
#define RETURN_FALSE_ON_FAILED_HR(x) { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s(%d) %s failed with 0x%x\n", __FILE__, __LINE__, #x, (unsigned int)hr_); return false; } }

class Encoder
{
public:
    Encoder(Header inHeader, Header outHeader, bool hardware, std::string outputFile, int bitrate)
        : inWidth(inHeader.width)
        , inHeight(inHeader.height)
        , hardware(hardware)
    {
        // ------------------------------------------------------------------------
        // Initialize COM and Media Foundation
        // ------------------------------------------------------------------------

        CHECK_HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
        CHECK_HR(MFStartup(MF_VERSION));

        // ------------------------------------------------------------------------
        // Initialize D3D11
        // ------------------------------------------------------------------------
        if (hardware)
        {
            CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

            UINT index = 0;
            HRESULT adapterHr;
            while (true)
            {
                adapterHr = factory->EnumAdapters(index++, &adapter);
                if (FAILED(adapterHr))
                    break;

                CHECK_HR(adapter->GetDesc(&desc));

                // Check for software adapter
                if (desc.VendorId == 0x1002 || desc.VendorId == 0x10DE)
                {
                    break;
                }
                adapter.Release();
                ZeroMemory(&desc, sizeof(desc));
            }

            D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
            CHECK_HR(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, featureLevels, 4, D3D11_SDK_VERSION, &device, NULL, &context));
            /*
            {
                // Probably not necessary in this application, but maybe the MFT requires it?
                CComQIPtr<ID3D10Multithread> mt(device);
                CHECK(mt);
                mt->SetMultithreadProtected(TRUE);
            }
            */
            // Create device manager
            UINT resetToken;
            CHECK_HR(MFCreateDXGIDeviceManager(&resetToken, &deviceManager));
            CHECK_HR(deviceManager->ResetDevice(device, resetToken));
        }

        // ------------------------------------------------------------------------
        // Initialize hardware encoder MFT
        // ------------------------------------------------------------------------

        {
            // Find the encoder
            CComHeapPtr<IMFActivate*> activateRaw;
            UINT32 activateCount = 0;

            // Input & output types
            MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
            MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };

            //CComPtr<IMFAttributes> enumAttrs;
            //CHECK_HR(MFCreateAttributes(&enumAttrs, 1));
            //CHECK_HR(enumAttrs->SetBlob(MFT_ENUM_ADAPTER_LUID, (BYTE*)&desc.AdapterLuid, sizeof(LUID)));

            CHECK_HR(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, hardware ? MFT_ENUM_FLAG_HARDWARE : 0, &inInfo, &outInfo, &activateRaw, &activateCount));

            CHECK(activateCount != 0);

            // Choose the first returned encoder
            CComPtr<IMFActivate> activate = activateRaw[hardware ? 1 : 0];

            // Print name
            UINT32 nameLength;
            std::wstring name;
            CHECK_HR(activate->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength));
            // IMFAttributes::GetString returns a null-terminated wide string
            name.resize((size_t)nameLength + 1);
            CHECK_HR(activate->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], (UINT32)name.size(), &nameLength));
            name.resize(nameLength);
            printf("Using %ls\n", name.c_str());

            // Activate
            CHECK_HR(activate->ActivateObject(IID_PPV_ARGS(&transform)));

            // Memory management
            for (UINT32 i = 0; i < activateCount; i++)
                activateRaw[i]->Release();

            // Get attributes
            CHECK_HR(transform->GetAttributes(&transformAttrs));
        }


        // ------------------------------------------------------------------------
        // Config the transform
        // ------------------------------------------------------------------------

        {
            CComPtr<ICodecAPI> codecApi;
            CHECK_HR(transform->QueryInterface(IID_PPV_ARGS(&codecApi)));

            /*
            VARIANT rateControlMode;
            rateControlMode.vt = VT_UI4;
            rateControlMode.ulVal = eAVEncCommonRateControlMode_CBR;
            CHECK_HR(codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &rateControlMode));

            VARIANT meanBitrate;
            meanBitrate.vt = VT_UI4;
            meanBitrate.ulVal = bitrate;
            HRESULT hr_range = (codecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &meanBitrate));
            */

            //VARIANT quality;
            //quality.vt = VT_UI4;
            //quality.ulVal = 0;
            //HRESULT hr_range = (codecApi->SetValue(&CODECAPI_AVEncCommonQuality, &quality));

            //CHECK_HR(transformAttrs->SetUINT32(MF_LOW_LATENCY, TRUE));
            if (hardware)
            {
                // Unlock the transform for async use and get event generator
                CHECK_HR(transformAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
                CHECK(eventGen = transform);
                CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p)));
            }
        }

        // Get stream IDs (expect 1 input and 1 output stream)
        {
            HRESULT hr = transform->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
            if (hr == E_NOTIMPL)
            {
                inputStreamID = 0;
                outputStreamID = 0;
                hr = S_OK;
            }
            CHECK_HR(hr);
        }

        // ------------------------------------------------------------------------
        // Configure hardware encoder MFT
        // ------------------------------------------------------------------------

        // Set output type
        CComPtr<IMFMediaType> outputType;
        CHECK_HR(MFCreateMediaType(&outputType));

        CHECK_HR(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK_HR(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        CHECK_HR(outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate));
        CHECK_HR(MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, outHeader.width, outHeader.height));
        CHECK_HR(MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, outHeader.frameRate.num, outHeader.frameRate.den));
        CHECK_HR(outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2));
        CHECK_HR(outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));

        CHECK_HR(transform->SetOutputType(outputStreamID, outputType, 0));

        // Set input type
        CComPtr<IMFMediaType> inputType;
        CHECK_HR(transform->GetInputAvailableType(inputStreamID, 0, &inputType));

        CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
        CHECK_HR(inputType->SetUINT32(MF_MT_INTERLACE_MODE, 2));
        CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, inWidth, inHeight));
        CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, inHeader.frameRate.num, inHeader.frameRate.den));

        CHECK_HR(transform->SetInputType(inputStreamID, inputType, 0));


        // ------------------------------------------------------------------------
        // Create sample allocator
        // ------------------------------------------------------------------------
        /*
        {
            MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&allocator));
            CHECK(allocator);

            CComPtr<IMFAttributes> allocAttrs;
            MFCreateAttributes(&allocAttrs, 2);

            CHECK_HR(allocAttrs->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET));
            CHECK_HR(allocAttrs->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT));

            CHECK_HR(allocator->SetDirectXManager(deviceManager));
            CHECK_HR(allocator->InitializeSampleAllocatorEx(1, 2, allocAttrs, inputType));
        }
        */

        // ------------------------------------------------------------------------
        // Open the output file
        // ------------------------------------------------------------------------

        fout.open(outputFile, std::ios::binary | std::ios::out | std::ios::trunc);

        // ------------------------------------------------------------------------
        // Start encoding
        // ------------------------------------------------------------------------

        //CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
        CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
        CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));
    }

    void hwEncodeImpl(FrameParser& parser)
    {
        for (;;)
        {
            // Get next event
            CComPtr<IMFMediaEvent> event;
            CHECK_HR(eventGen->GetEvent(0, &event));

            MediaEventType eventType;
            CHECK_HR(event->GetType(&eventType));

            switch (eventType)
            {
            case METransformNeedInput:
            {
                auto frame = parser.readFrame();
                if (!frame)
                {
                    return;
                }
                
                CComPtr<IMFSample> sample;
                CHECK_HR(MFCreateSample(&sample));

                // Generate texture
                CComPtr<ID3D11Texture2D> texture;
                D3D11_TEXTURE2D_DESC desc;
                ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));

                desc.Format = DXGI_FORMAT_NV12;
                desc.Width = inWidth;
                desc.Height = inHeight;
                desc.MipLevels = 1;
                desc.ArraySize = 1;
                desc.SampleDesc.Count = 1;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                desc.Usage = D3D11_USAGE_DYNAMIC;
                desc.BindFlags = D3D11_BIND_VIDEO_ENCODER;

                HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
                if (FAILED(hr))
                {
                    return;
                }

                D3D11_MAPPED_SUBRESOURCE mappedResource;
                ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));
                //DWORD length = inWidth * inHeight * 3 / 2;
                // Lock texture
                CHECK_HR(context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));
                //  Update the vertex buffer here.
                BYTE* srcY = frame->yuv.data();
                BYTE* dstY = (BYTE*)mappedResource.pData;
                for (int y = 0; y < inHeight; ++y) {
                    memcpy(dstY + y * mappedResource.RowPitch, srcY + y * inWidth, inWidth);
                }
                BYTE* srcUV = srcY + inWidth * inHeight;
                BYTE* dstUV = (BYTE*)mappedResource.pData + mappedResource.RowPitch * inHeight;
                for (int y = 0; y < inHeight / 2; ++y) {
                    memcpy(dstUV + y * mappedResource.RowPitch, srcUV + y * inWidth, inWidth);
                }
                //  Reenable GPU access to the vertex buffer data.
                context->Unmap(texture, 0);

                // Create media buffer backed by DXGI
                CComPtr<IMFMediaBuffer> dxgiMediaBuffer;
                CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0, FALSE, &dxgiMediaBuffer));
                CHECK_HR(sample->AddBuffer(dxgiMediaBuffer));

                // Other fields for sample
                CHECK_HR(sample->SetSampleTime(frame->pts));
                CHECK_HR(sample->SetSampleDuration(frame->duration));

                CHECK_HR(transform->ProcessInput(inputStreamID, sample, 0));

                // Dereferencing the device once after feeding each frame "fixes" the leak.
                //device.p->Release();

                break;
            }

            case METransformHaveOutput:
            {
                DWORD status;
                MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
                outputBuffer.dwStreamID = outputStreamID;

                HRESULT hr = (transform->ProcessOutput(0, 1, &outputBuffer, &status));
                if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
                {
                    printf("\n\n!!!!! Request Stream Change !!!!!\n\n");
                }
                else if (FAILED(hr))
                {
                    printf("ProcessOutput failed: 0x%x\n", (unsigned int)hr);
                    return;
                }

                DWORD bufCount;
                DWORD bufLength;
                CHECK_HR(outputBuffer.pSample->GetBufferCount(&bufCount));

                CComPtr<IMFMediaBuffer> outBuffer;
                CHECK_HR(outputBuffer.pSample->GetBufferByIndex(0, &outBuffer));
                CHECK_HR(outBuffer->GetCurrentLength(&bufLength));

                printf("METransformHaveOutput buffers=%lu, bytes=%lu\n", bufCount, bufLength);

                // write bytes to file
                BYTE* encodedData;
                DWORD encodedLength;
                CHECK_HR(outBuffer->Lock(&encodedData, nullptr, &encodedLength));
                fout.write((char*)encodedData, encodedLength);
                CHECK_HR(outBuffer->Unlock());

                // Release the sample as it is not processed further.
                if (outputBuffer.pSample)
                    outputBuffer.pSample->Release();
                if (outputBuffer.pEvents)
                    outputBuffer.pEvents->Release();
                break;
            }
            }
        }
    }

    bool swSendFrame(FrameParser& parser)
    {
        DWORD flags = 0;
        RETURN_FALSE_ON_FAILED_HR(transform->GetInputStatus(0, &flags));
        if ((flags & MFT_INPUT_STATUS_ACCEPT_DATA) == false)
        {
            return true;
        }

        auto frame = parser.readFrame();
        if (!frame)
        {
            return false;
        }
        CComPtr<IMFSample> sample;
        CComPtr<IMFMediaBuffer> memoryBuffer;
        RETURN_FALSE_ON_FAILED_HR(MFCreateAlignedMemoryBuffer(frame->yuv.size(), MF_16_BYTE_ALIGNMENT, &memoryBuffer));
        RETURN_FALSE_ON_FAILED_HR(MFCreateSample(&sample));
        BYTE* bufferData;
        memoryBuffer->Lock(&bufferData, nullptr, nullptr);
        memoryBuffer->SetCurrentLength(frame->yuv.size());
        memcpy(bufferData, frame->yuv.data(), frame->yuv.size());
        memoryBuffer->Unlock();
        sample->AddBuffer(memoryBuffer);
        // Other fields for sample
        RETURN_FALSE_ON_FAILED_HR(sample->SetSampleTime(frame->pts));
        RETURN_FALSE_ON_FAILED_HR(sample->SetSampleDuration(frame->duration));
        RETURN_FALSE_ON_FAILED_HR(transform->ProcessInput(inputStreamID, sample, 0));
        return true;
    }

    bool swReceivePacket()
    {
        MFT_OUTPUT_STREAM_INFO streamInfo;
        RETURN_FALSE_ON_FAILED_HR(transform->GetOutputStreamInfo(0, &streamInfo));
        CComPtr<IMFMediaBuffer> mediaBuffer;
        CComPtr<IMFSample> sampleOut;
        RETURN_FALSE_ON_FAILED_HR(MFCreateSample(&sampleOut));
        RETURN_FALSE_ON_FAILED_HR(MFCreateMemoryBuffer(streamInfo.cbSize, &mediaBuffer));
        RETURN_FALSE_ON_FAILED_HR(sampleOut->AddBuffer(mediaBuffer));

        MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
        outputBuffer.dwStreamID = outputStreamID;
        outputBuffer.pSample = sampleOut;
        DWORD status;

        HRESULT hr = (transform->ProcessOutput(0, 1, &outputBuffer, &status));
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        {
            return false;
        }
        else if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            printf("\n\n!!!!! Request Stream Change !!!!!\n\n");
        }
        else
        {
            RETURN_FALSE_ON_FAILED_HR(hr);
        }

        DWORD bufCount;
        DWORD bufLength;
        RETURN_FALSE_ON_FAILED_HR(outputBuffer.pSample->GetBufferCount(&bufCount));

        CComPtr<IMFMediaBuffer> outBuffer;
        RETURN_FALSE_ON_FAILED_HR(outputBuffer.pSample->GetBufferByIndex(0, &outBuffer));
        RETURN_FALSE_ON_FAILED_HR(outBuffer->GetCurrentLength(&bufLength));

        printf("METransformHaveOutput buffers=%lu, bytes=%lu\n", bufCount, bufLength);

        // write bytes to file
        BYTE* encodedData;
        DWORD encodedLength;
        RETURN_FALSE_ON_FAILED_HR(outBuffer->Lock(&encodedData, nullptr, &encodedLength));
        fout.write((char*)encodedData, encodedLength);
        RETURN_FALSE_ON_FAILED_HR(outBuffer->Unlock());

        // Release the sample as it is not processed further.
        if (outputBuffer.pEvents)
            outputBuffer.pEvents->Release();
        return true;
    }

    void swEncodeImpl(FrameParser& parser)
    {
        bool sendFrameRunning = true;
        bool receivePacketRunning = true;
        while (sendFrameRunning || receivePacketRunning)
        {
            if (sendFrameRunning)
                sendFrameRunning = swSendFrame(parser);
            if (receivePacketRunning)
                receivePacketRunning = swReceivePacket() || sendFrameRunning;
        }
    }

    void encode(FrameParser& parser)
    {
        hardware ? hwEncodeImpl(parser) : swEncodeImpl(parser);
    }

    ~Encoder()
    {
        // ------------------------------------------------------------------------
        // Finish encoding
        // ------------------------------------------------------------------------

        CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL));
        CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL));
        CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL));

        // Shutdown
        printf("Finished encoding\n");

        // I've tried all kinds of things...
        //CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(nullptr)));

        //transform->SetInputType(inputStreamID, NULL, 0);
        //transform->SetOutputType(outputStreamID, NULL, 0);

        //transform->DeleteInputStream(inputStreamID);

        //deviceManager->ResetDevice(NULL, resetToken);

        CHECK_HR(MFShutdownObject(transform));
        fout.close();
    }

private:
    int inWidth, inHeight;
    bool hardware;
    std::ofstream fout;

    DXGI_ADAPTER_DESC desc;
    CComPtr<IDXGIFactory1> factory;
    CComPtr<IDXGIAdapter> adapter;
    CComPtr<ID3D11Device> device;
    CComPtr<ID3D11DeviceContext> context;
    CComPtr<IMFDXGIDeviceManager> deviceManager;

    CComPtr<IMFVideoSampleAllocatorEx> allocator;
    CComPtr<IMFTransform> transform;
    CComPtr<IMFAttributes> transformAttrs;
    CComQIPtr<IMFMediaEventGenerator> eventGen;
    DWORD inputStreamID;
    DWORD outputStreamID;
};
