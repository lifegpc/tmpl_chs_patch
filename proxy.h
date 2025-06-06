#pragma once
#include <windows.h>
#include <d3d9.h>

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

    static inline decltype(Direct3DCreate9)* OriginalDirect3DCreate9{};
};
