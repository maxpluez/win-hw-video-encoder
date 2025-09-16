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
#include <thread>

// Windows
#include <windows.h>
#include <atlbase.h>

// DirectX
#include <dxgi.h>
#include <d3d11.h>

// Media Foundation
#include <evr.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>

// Error handling
#define CHECK(x) if (!(x)) { printf("%s(%d) %s was false\n", __FILE__, __LINE__, #x); throw std::exception(); }
#define CHECK_HR(x) { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s(%d) %s failed with 0x%x\n", __FILE__, __LINE__, #x, hr_); throw std::exception(); } }

class Callback : public IMFAsyncCallback
{
public:
    Callback() = default;
    virtual ~Callback() = default;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD* pdwFlags, DWORD* pdwQueue) override
    {
        *pdwFlags = /*MFASYNC_BLOCKING_CALLBACK*/ 0;
        *pdwQueue = MFASYNC_CALLBACK_QUEUE_STANDARD;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* pResult) override
    {
        std::cout << "Callback invoked" << std::endl;
        IUnknown* pUnk = nullptr;
        HRESULT hr = pResult->GetObject(&pUnk);
        if (SUCCEEDED(hr))
        {
            pUnk->Release();
        }
        return S_OK;
    }
};

int main()
{
    std::cout << "Start" << std::endl;
    MFStartup(MF_VERSION);

    std::unique_ptr<Callback> callback = std::make_unique<Callback>();

    // ------------------------------------------------------------------------
    // Initialize D3D11
    // ------------------------------------------------------------------------

    CComPtr<IDXGIFactory1> factory;
    CComPtr<IDXGIAdapter> adapter;
    CComPtr<ID3D11Device> device;
    CComPtr<ID3D11DeviceContext> context;
    DXGI_ADAPTER_DESC adapterDesc;

    CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    UINT index = 0;
    HRESULT adapterHr;
    while (true)
    {
        adapterHr = factory->EnumAdapters(index++, &adapter);
        if (FAILED(adapterHr))
            break;

        CHECK_HR(adapter->GetDesc(&adapterDesc));

        // Check for software adapter
        if (adapterDesc.VendorId == 0x1002 || adapterDesc.VendorId == 0x10DE)
        {
            break;
        }
    }

    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    CHECK_HR(D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG, featureLevels, 4, D3D11_SDK_VERSION, &device, NULL, &context));

    CComPtr<ID3D11Texture2D> texture;
    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = 1280;
    desc.Height = 720;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    CHECK_HR(device->CreateTexture2D(&desc, nullptr, &texture));

    IMFSample* sample;
    IMFTrackedSample* trackedSample;
    HRESULT hr = MFCreateVideoSampleFromSurface(nullptr, &sample);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create tracked sample" << std::endl;
        return 1;
    }

    CComPtr<IMFMediaBuffer> dxgiMediaBuffer;
    CHECK_HR(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), texture, 0, FALSE, &dxgiMediaBuffer));
    sample->AddBuffer(dxgiMediaBuffer.p);

    sample->QueryInterface(IID_PPV_ARGS(&trackedSample));
    trackedSample->SetAllocator(callback.get(), nullptr);

    trackedSample->Release();
    sample->Release();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << "End" << std::endl;
}
