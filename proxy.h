#pragma once
#include <windows.h>
//#include <ctffunc.h>
#include <d2d1.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <ddraw.h>
#include <dsound.h>
#include <dwrite.h>
#include <gdiplus.h>
#include <Mmreg.h>
#include <msctf.h>
#include <MSAcm.h>

#include <codecvt>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <sstream>
#include <vector>

class Proxy
{
public:
    static void Init(HMODULE hProxy);

    static inline HMODULE ProxyModuleHandle{};
    static inline HMODULE OriginalModuleHandle{};

    static inline void* OriginalDirect3DCreate9{};
};
