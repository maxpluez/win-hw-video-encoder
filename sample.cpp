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
#include <mfidl.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>

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
    std::unique_ptr<Callback> callback = std::make_unique<Callback>();
    IMFTrackedSample* trackedSample = nullptr;
    HRESULT hr = MFCreateTrackedSample(&trackedSample);
    if (FAILED(hr))
    {
        std::cerr << "Failed to create tracked sample" << std::endl;
        return 1;
    }
    trackedSample->SetAllocator(callback.get(), nullptr);
    trackedSample->Release();
}
