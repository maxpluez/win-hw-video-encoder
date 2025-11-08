#pragma once

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Winmm.lib")

// std
#include <string>

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
#define CHECK(x) if (!(x)) { printf("%s(%d) %s was false\n", __FILE__, __LINE__, #x); exit(1); }
#define CHECK_HR(x) { HRESULT hr_ = (x); if (FAILED(hr_)) { printf("%s(%d) %s failed with 0x%x\n", __FILE__, __LINE__, #x, (unsigned int)hr_); exit(1); } }

class Decoder
{
public:
    Decoder()
    {
        // ------------------------------------------------------------------------
        // Initialize COM and Media Foundation
        // ------------------------------------------------------------------------

        CHECK_HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
        CHECK_HR(MFStartup(MF_VERSION));

        // Additional decoder initialization code would go here
        CComHeapPtr<IMFActivate*> activateRaw;
        UINT32 activateCount = 0;

        // Input & output types
        MFT_REGISTER_TYPE_INFO inInfo = { MFMediaType_Video, MFVideoFormat_HEVC };
        MFT_REGISTER_TYPE_INFO outInfo = { MFMediaType_Video, MFVideoFormat_NV12 };

        //CComPtr<IMFAttributes> enumAttrs;
        //CHECK_HR(MFCreateAttributes(&enumAttrs, 1));
        //CHECK_HR(enumAttrs->SetBlob(MFT_ENUM_ADAPTER_LUID, (BYTE*)&desc.AdapterLuid, sizeof(LUID)));

        CHECK_HR(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, 0, &inInfo, &outInfo, &activateRaw, &activateCount));

        CHECK(activateCount != 0);

        for (int activateIndex = 0; activateIndex < activateCount; ++activateIndex)
        {
            // Choose the first returned encoder
            CComPtr<IMFActivate> activate = activateRaw[activateIndex];
            CHECK(activate);

            // Print name
            UINT32 nameLength;
            std::wstring name;
            CHECK_HR(activate->GetStringLength(MFT_FRIENDLY_NAME_Attribute, &nameLength));
            // IMFAttributes::GetString returns a null-terminated wide string
            name.resize((size_t)nameLength + 1);
            CHECK_HR(activate->GetString(MFT_FRIENDLY_NAME_Attribute, &name[0], (UINT32)name.size(), &nameLength));
            name.resize(nameLength);
            printf("Activating %ls\n", name.c_str());

            // Activate
            HRESULT hr = activate->ActivateObject(IID_PPV_ARGS(&transform));
            if (SUCCEEDED(hr))
            {
                break;
            }
            activate->ShutdownObject();
            transform.Release();
        }

        // Memory management
        for (UINT32 i = 0; i < activateCount; i++)
            activateRaw[i]->Release();
    }

private:
    CComPtr<IMFTransform> transform;
};
