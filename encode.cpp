#include <chrono>
#include <thread>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")

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

// Error handling
#define CHECK(x) if (!(x)) { printf("%s(%d) %s was false\n", __FILE__, __LINE__, #x); throw std::exception(); }
#define CHECK_HR(x) { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s(%d) %s failed with 0x%x\n", __FILE__, __LINE__, #x, hr_); throw std::exception(); } }

// Constants
constexpr UINT ENCODE_WIDTH = 1280;
constexpr UINT ENCODE_HEIGHT = 720;
//constexpr UINT ENCODE_FRAMES = 60;
constexpr UINT64 mfDuration = /*10000000 / ENCODE_FRAMES*/ 16000;
UINT64 mfTicks = 0;

class Encoder : public IMFAsyncCallback
{
public:
    Encoder()
    {
        // ------------------------------------------------------------------------
        // Open File
        // ------------------------------------------------------------------------

        fout.open("vid.h264", std::ios::binary | std::ios::out | std::ios::trunc);

        // ------------------------------------------------------------------------
        // Initialize D3D11
        // ------------------------------------------------------------------------

        CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        UINT index = 0;
        HRESULT adapterHr;
        while (true)
        {
            adapterHr = factory->EnumAdapters(index++, &adapter);
            if (FAILED(adapterHr))
            {
                printf("adapter not found\n");
                break;
            }

            CHECK_HR(adapter->GetDesc(&desc));
            break;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        CHECK_HR(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT, featureLevels, 4, D3D11_SDK_VERSION, &device11, NULL, &context11));

        {
            // Probably not necessary in this application, but maybe the MFT requires it?
            //CComQIPtr<ID3D10Multithread> mt{device11};
            //CHECK(mt);
            //mt->SetMultithreadProtected(TRUE);
        }

        // Create device manager
        UINT resetToken;
        CHECK_HR(MFCreateDXGIDeviceManager(&resetToken, &deviceManager));
        CHECK_HR(deviceManager->ResetDevice(device11.p, resetToken));


        // ------------------------------------------------------------------------
        // Initialize hardware encoder MFT
        // ------------------------------------------------------------------------

        {
            // Find the encoder
            CComHeapPtr<IMFActivate*> activateRaw;
            UINT32 activateCount = 0;

            // Input & output types
            MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_ARGB32 };
            MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_H264 };

            //CComPtr<IMFAttributes> enumAttrs;
            //CHECK_HR(MFCreateAttributes(&enumAttrs, 1));
            //CHECK_HR(enumAttrs->SetBlob(MFT_ENUM_ADAPTER_LUID, (BYTE*)&desc.AdapterLuid, sizeof(LUID)));

            CHECK_HR(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE, &inInfo, &outInfo, &activateRaw, &activateCount));

            CHECK(activateCount != 0);

            // Choose the first returned encoder
            CComPtr<IMFActivate> activate = activateRaw[0];

            // Activate
            CHECK_HR(activate->ActivateObject(IID_PPV_ARGS(&processor)));

            // Get attributes
            CHECK_HR(processor->GetAttributes(&processorAttrs));
            CHECK_HR(processorAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
            CHECK(events = processor);
            CHECK_HR(processor->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p)));

            // Memory management
            //for (UINT32 i = 0; i < activateCount; i++)
            //    activateRaw[i]->Release();
        }


        // ------------------------------------------------------------------------
        // Query encoder name (not necessary, but nice) and unlock for async use
        // ------------------------------------------------------------------------
        /*
        {
            UINT32 nameLength;
            std::wstring name;

            CHECK_HR(processorAttrs->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength));

            // IMFAttributes::GetString returns a null-terminated wide string
            name.resize((size_t)nameLength + 1);
            CHECK_HR(processorAttrs->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], (UINT32)name.size(), &nameLength));
            name.resize(nameLength);

            printf("Using %ls\n", name.c_str());

            // Unlock the transform for async use and get event generator
            CHECK_HR(processorAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
            CHECK(events = processor);
        }
        */

        // Get stream IDs (expect 1 input and 1 output stream)
        {
            HRESULT hr = processor->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
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

        CHECK_HR(outputType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High));
        CHECK_HR(outputType->SetUINT32(MF_MT_MPEG2_LEVEL, eAVEncH264VLevel4_2));
        CHECK_HR(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK_HR(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        CHECK_HR(outputType->SetUINT32(MF_MT_AVG_BITRATE, 4000000));
        CHECK_HR(MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
        //CHECK_HR(MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, 60, 1));
        CHECK_HR(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        CHECK_HR(outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));

        CHECK_HR(processor->SetOutputType(outputStreamID, outputType, 0));

        // Set input type
        CComPtr<IMFMediaType> inputType;
        CHECK_HR(processor->GetInputAvailableType(inputStreamID, 0, &inputType));

        CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_ARGB32));
        CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
        CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 30, 1));

        CHECK_HR(processor->SetInputType(inputStreamID, inputType, 0));


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
        // Start encoding
        // ------------------------------------------------------------------------
        events->BeginGetEvent(this, nullptr);

        //CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
        CHECK_HR(processor->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
        CHECK_HR(processor->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));   
    }

    ~Encoder()
    {
        fout.close();
    }

    void stop()
    {
        processor->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        processor->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    }

    // dummy IUnknown impl
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 1; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 1; }

    // return values based on https://learn.microsoft.com/en-us/windows/win32/api/mfobjects/nf-mfobjects-imfasynccallback-getparameters
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) override
    {
        // we return immediately and don't do anything except signaling another thread
        *pdwFlags = /*MFASYNC_BLOCKING_CALLBACK*/ 0;
        *pdwQueue = MFASYNC_CALLBACK_QUEUE_STANDARD;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* pAsyncResult) override
    {
        HRESULT hr = S_OK;

        CComPtr<IMFMediaEvent> event;
        CHECK_HR(events->EndGetEvent(pAsyncResult, &event));

        MediaEventType eventType;
        CHECK_HR(event->GetType(&eventType));

        switch (eventType)
        {
        case METransformNeedInput:
        {
            // Generate texture
            CComPtr<ID3D11Texture2D> texture;
            D3D11_TEXTURE2D_DESC desc;
            ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));

            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.Width = ENCODE_WIDTH;
            desc.Height = ENCODE_HEIGHT;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            HRESULT hr = device11->CreateTexture2D(&desc, nullptr, &texture);
            if (FAILED(hr))
            {
                printf("?\n");
                break;
            }

            D3D11_MAPPED_SUBRESOURCE mappedResource;
            ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));
            DWORD length = ENCODE_WIDTH * ENCODE_HEIGHT * 4;
            // Lock texture
            CHECK_HR(context11->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));
            //  Update the vertex buffer here.
            memset(mappedResource.pData, 200, length);
            //  Reenable GPU access to the vertex buffer data.
            context11->Unmap(texture, 0);

            // Create media buffer backed by DXGI
            IMFMediaBuffer* dxgiMediaBuffer;
            CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0, FALSE, &dxgiMediaBuffer));

            // Create sample
            IMFSample* dxgiSample;
            CHECK_HR(MFCreateSample(&dxgiSample));
            CHECK_HR(dxgiSample->AddBuffer(dxgiMediaBuffer));

            // Other fields for sample
            mfTicks += mfDuration;
            CHECK_HR(dxgiSample->SetSampleTime(mfTicks));
            hr = (dxgiSample->SetSampleDuration(mfDuration));
            if (FAILED(hr))
            {
                printf("??\n");
                break;
            }

            CHECK_HR(processor->ProcessInput(inputStreamID, dxgiSample, 0));

            // Dereferencing the device once after feeding each frame "fixes" the leak.
            //device.p->Release();

            break;
        }

        case METransformHaveOutput:
        {
            DWORD status;
            MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
            outputBuffer.dwStreamID = outputStreamID;

            hr = (processor->ProcessOutput(0, 1, &outputBuffer, &status));
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
            {
                // Stream format change.
                hr = processor->GetStreamIDs(1, &inputStreamID, 1, &outputStreamID);
                if (hr == E_NOTIMPL)
                {
                    inputStreamID = 0;
                    outputStreamID = 0;
                    hr = S_OK;
                }

                CComPtr<IMFMediaType> availableOutputType;
                for (DWORD typeIndex = 0;; ++typeIndex)
                {
                    CHECK_HR(processor->GetOutputAvailableType(outputStreamID, typeIndex, &availableOutputType));
                    
                    // Check if the type is H264
                    GUID majorType, subType;
                    availableOutputType->GetMajorType(&majorType);
                    availableOutputType->GetGUID(MF_MT_SUBTYPE, &subType);
                    if (majorType == MFMediaType_Video && subType == MFVideoFormat_H264)
                    {
                        // found
                        break;
                    }
                    availableOutputType.Release();
                }

                UINT32 width, height, bitrate, frameNumerator, frameDenominator;
                MFGetAttributeSize(availableOutputType.p, MF_MT_FRAME_SIZE, &width, &height);
                MFGetAttributeRatio(availableOutputType.p, MF_MT_FRAME_RATE, &frameNumerator, &frameDenominator);
                availableOutputType->GetUINT32(MF_MT_AVG_BITRATE, &bitrate);
                // Set the new type
                CHECK_HR(processor->SetOutputType(outputStreamID, availableOutputType.p, 0));
                break;
            }
            CHECK_HR(hr);

            DWORD bufCount;
            DWORD bufLength;
            CHECK_HR(outputBuffer.pSample->GetBufferCount(&bufCount));

            CComPtr<IMFMediaBuffer> outBuffer;
            CHECK_HR(outputBuffer.pSample->GetBufferByIndex(0, &outBuffer));
            CHECK_HR(outBuffer->GetCurrentLength(&bufLength));

            printf("METransformHaveOutput buffers=%d, bytes=%d\n", bufCount, bufLength);

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

        case METransformDrainComplete:
        {
            return hr;
        }
        }

        CHECK_HR(events->BeginGetEvent(this, nullptr));

        return hr;
    }

private:
    std::ofstream fout;

    DXGI_ADAPTER_DESC desc;
    CComPtr<IDXGIFactory1> factory;
    CComPtr<IDXGIAdapter> adapter;
    CComPtr<IMFActivate> activate;
    CComPtr<IMFTransform> processor;
    CComPtr<IMFAttributes> processorAttrs;
    CComPtr<IMFTransform> dsp;
    CComQIPtr<IMFMediaEventGenerator> events;
    CComPtr<IMFDXGIDeviceManager> deviceManager;
    CComPtr<ID3D11Device> device11;
    CComPtr<ID3D11DeviceContext> context11;
    GUID encoderInputFrameFormat = MFVideoFormat_ARGB32;

    DWORD inputStreamID;
    DWORD outputStreamID;
};

void runEncode();

int main()
{
    CHECK_HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    CHECK_HR(MFStartup(MF_VERSION));

    Encoder encoder;
    std::this_thread::sleep_for(std::chrono::seconds(5));
    encoder.stop();

    CHECK_HR(MFShutdown());

    return 0;
}

void runEncode()
{
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

    // ------------------------------------------------------------------------
    // Open File
    // ------------------------------------------------------------------------

    std::ofstream fout;
    fout.open("vid.h264", std::ios::binary | std::ios::out | std::ios::trunc);

    // ------------------------------------------------------------------------
    // Initialize D3D11
    // ------------------------------------------------------------------------

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
    }

    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    CHECK_HR(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG, featureLevels, 4, D3D11_SDK_VERSION, &device, NULL, &context));

    {
        // Probably not necessary in this application, but maybe the MFT requires it?
        CComQIPtr<ID3D10Multithread> mt(device);
        CHECK(mt);
        mt->SetMultithreadProtected(TRUE);
    }

    // Create device manager
    UINT resetToken;
    CHECK_HR(MFCreateDXGIDeviceManager(&resetToken, &deviceManager));
    CHECK_HR(deviceManager->ResetDevice(device, resetToken));


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

        CHECK_HR(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE, &inInfo, &outInfo, &activateRaw, &activateCount));

        CHECK(activateCount != 0);

        // Choose the first returned encoder
        CComPtr<IMFActivate> activate = activateRaw[1];

        // Memory management
        for (UINT32 i = 0; i < activateCount; i++)
            activateRaw[i]->Release();

        // Activate
        CHECK_HR(activate->ActivateObject(IID_PPV_ARGS(&transform)));

        // Get attributes
        CHECK_HR(transform->GetAttributes(&transformAttrs));
    }


    // ------------------------------------------------------------------------
    // Query encoder name (not necessary, but nice) and unlock for async use
    // ------------------------------------------------------------------------

    {
        UINT32 nameLength;
        std::wstring name;

        CHECK_HR(transformAttrs->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength));

        // IMFAttributes::GetString returns a null-terminated wide string
        name.resize((size_t)nameLength + 1);
        CHECK_HR(transformAttrs->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], (UINT32)name.size(), &nameLength));
        name.resize(nameLength);

        printf("Using %ls\n", name.c_str());

        // Unlock the transform for async use and get event generator
        CHECK_HR(transformAttrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE));
        CHECK(eventGen = transform);
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

    // Set D3D manager
    CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(deviceManager.p)));

    // Set output type
    CComPtr<IMFMediaType> outputType;
    CHECK_HR(MFCreateMediaType(&outputType));

    CHECK_HR(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK_HR(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    CHECK_HR(outputType->SetUINT32(MF_MT_AVG_BITRATE, 30000000));
    CHECK_HR(MFSetAttributeSize(outputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
    CHECK_HR(MFSetAttributeRatio(outputType, MF_MT_FRAME_RATE, 60, 1));
    CHECK_HR(outputType->SetUINT32(MF_MT_INTERLACE_MODE, 2));
    CHECK_HR(outputType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));

    CHECK_HR(transform->SetOutputType(outputStreamID, outputType, 0));

    // Set input type
    CComPtr<IMFMediaType> inputType;
    CHECK_HR(transform->GetInputAvailableType(inputStreamID, 0, &inputType));

    CHECK_HR(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK_HR(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    CHECK_HR(MFSetAttributeSize(inputType, MF_MT_FRAME_SIZE, ENCODE_WIDTH, ENCODE_HEIGHT));
    CHECK_HR(MFSetAttributeRatio(inputType, MF_MT_FRAME_RATE, 60, 1));

    CHECK_HR(transform->SetInputType(inputStreamID, inputType, 0));


    // ------------------------------------------------------------------------
    // Create sample allocator
    // ------------------------------------------------------------------------

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


    // ------------------------------------------------------------------------
    // Start encoding
    // ------------------------------------------------------------------------

    CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
    CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
    CHECK_HR(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));

    // Encode loop
    /*
    for (int i = 0; i < ENCODE_FRAMES; i++)
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
            // Generate texture
            CComPtr<ID3D11Texture2D> texture;
            D3D11_TEXTURE2D_DESC desc;
            ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));

            desc.Format = DXGI_FORMAT_NV12;
            desc.Width = ENCODE_WIDTH;
            desc.Height = ENCODE_HEIGHT;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.SampleDesc.Count = 1;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_VIDEO_ENCODER;

            HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
            if (FAILED(hr))
            {
                printf("?\n");
                return;
            }

            D3D11_MAPPED_SUBRESOURCE mappedResource;
            ZeroMemory(&mappedResource, sizeof(D3D11_MAPPED_SUBRESOURCE));
            DWORD length = ENCODE_WIDTH * ENCODE_HEIGHT * 3 / 2;
            // Lock texture
            CHECK_HR(context->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource));
            //  Update the vertex buffer here.
            memset(mappedResource.pData, 200, length);
            //  Reenable GPU access to the vertex buffer data.
            context->Unmap(texture, 0);

            // Create media buffer backed by DXGI
            IMFMediaBuffer* dxgiMediaBuffer;
            CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0, FALSE, &dxgiMediaBuffer));

            // Create sample
            IMFSample* dxgiSample;
            CHECK_HR(MFCreateSample(&dxgiSample));
            CHECK_HR(dxgiSample->AddBuffer(dxgiMediaBuffer));

            // Other fields for sample
            mfTicks += mfDuration;
            CHECK_HR(dxgiSample->SetSampleTime(mfTicks));
            CHECK_HR(dxgiSample->SetSampleDuration(mfDuration));

            CHECK_HR(transform->ProcessInput(inputStreamID, dxgiSample, 0));

            // Dereferencing the device once after feeding each frame "fixes" the leak.
            //device.p->Release();

            break;
        }

        case METransformHaveOutput:
        {
            DWORD status;
            MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
            outputBuffer.dwStreamID = outputStreamID;

            CHECK_HR(transform->ProcessOutput(0, 1, &outputBuffer, &status));

            DWORD bufCount;
            DWORD bufLength;
            CHECK_HR(outputBuffer.pSample->GetBufferCount(&bufCount));

            CComPtr<IMFMediaBuffer> outBuffer;
            CHECK_HR(outputBuffer.pSample->GetBufferByIndex(0, &outBuffer));
            CHECK_HR(outBuffer->GetCurrentLength(&bufLength));

            printf("METransformHaveOutput buffers=%d, bytes=%d\n", bufCount, bufLength);

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
    */

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