# win-video-hw-encoder
How to build?
1. Open Developer Powershell for VS 2019/2022.
    This Powershell has all environment variable for C++ headers and tools, without adding them to your system PATH.
    It can be found by searching for this as an app in Start, or as an option in your Windows terminal
2. Add cl.exe to your PATH.
    This is your compiler + linker
    For me it's under C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64
3. Run cl encode.cpp
    Add option /Zi if you want to generate a PDB file for debugging.
    Add option /EHsc to mute some warnings.
    For more details, check out https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170
4. Run ./encode.exe
