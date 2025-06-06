#include "proxy.h"

void Proxy::Init(HMODULE hProxy)
{
    ProxyModuleHandle = hProxy;
    
    OriginalModuleHandle = LoadLibraryW(L"d3d9-2.dll");
    if (OriginalModuleHandle == nullptr)
    {
        wchar_t realDllPath[MAX_PATH];
        GetSystemDirectoryW(realDllPath, MAX_PATH);
        wcscat_s(realDllPath, L"\\d3d9.dll");
        OriginalModuleHandle = LoadLibraryW(realDllPath);
        if (!OriginalModuleHandle) {
            MessageBoxW(nullptr, L"Cannot load original d3d9.dll library", L"Proxy", MB_ICONERROR);
            ExitProcess(0);
        }
    }
    OriginalDirect3DCreate9 = (decltype(Direct3DCreate9)*)GetProcAddress(OriginalModuleHandle, "Direct3DCreate9");
}

#if defined(_WIN64) || !defined(_MSVC_VER)
// 64位平台使用函数指针调用
extern "C" void FakeDirect3DCreate9()                            { ((void(*)())Proxy::OriginalDirect3DCreate9)(); }
#else
// 32位平台使用原有的内联汇编
__declspec(naked) void FakeDirect3DCreate9()                            { __asm { jmp [Proxy::OriginalDirect3DCreate9] } }
#endif
