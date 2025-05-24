#include <Windows.h>
#include "proxy.h"
#include "vfs.hpp"
#include "wchar_util.h"
#include "fileop.h"
#include "detours.h"
#include "m3t.h"
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <utility>
#include <vector>
#include <stdint.h>
#if MPV
#include "mpv/client.h"
#endif
#include "strings_dat.h"

static decltype(CreateFontIndirectA)* TrueCreateFontIndirectA = CreateFontIndirectA;
static HANDLE(WINAPI* TrueCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) = CreateFileW;
static BOOL(WINAPI* TrueReadFile)(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) = ReadFile;
static BOOL(WINAPI* TrueCloseHandle)(HANDLE hObject) = CloseHandle;
static DWORD(WINAPI* TrueGetFileSize)(HANDLE hFile, LPDWORD lpFileSizeHigh) = GetFileSize;
static decltype(GetFileSizeEx)* TrueGetFileSizeEx = GetFileSizeEx;
static DWORD(WINAPI* TrueSetFilePointer)(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) = SetFilePointer;
static decltype(SetFilePointerEx)* TrueSetFilePointerEx = SetFilePointerEx;
static decltype(GetFileType)* TrueGetFileType = GetFileType;
static decltype(GetFileAttributesW)* TrueGetFileAttributesW = GetFileAttributesW;
static decltype(GetFileAttributesExW)* TrueGetFileAttributesExW = GetFileAttributesExW;
static decltype(MessageBoxA)* TrueMessageBoxA = MessageBoxA;
static decltype(CreateWindowExA)* TrueCreateWindowExA = CreateWindowExA;
static decltype(LoadImageA)* TrueLoadImageA = LoadImageA;
static decltype(RegOpenKeyExA)* TrueRegOpenKeyExA = RegOpenKeyExA;
static decltype(RegCloseKey)* TrueRegCloseKey = RegCloseKey;
static decltype(RegQueryValueExA)* TrueRegQueryValueExA = RegQueryValueExA;

typedef int(__thiscall *DrawTextOn)(void* thisptr, int a, int b, int c, const char* buffer);
#if MPV
typedef HRESULT(__cdecl *OpenMedia)(LPCCH video);
typedef LPVOID(*ReleaseMedia)();
#endif

static VFS vfs;
static std::wstring wTitle;
static std::unordered_map<std::wstring, std::wstring> dialogMap;
static std::vector<std::pair<std::wregex, std::wstring>> dialogReList;
static std::unordered_set<HKEY> phkSet;
static std::unordered_map<std::string, std::string> stringsMap;

static decltype(sprintf)* sprintfFunc = nullptr;
static DrawTextOn DrawTextOnFunc = nullptr;
#if MPV
static OpenMedia OpenMediaFunc = nullptr;
static ReleaseMedia ReleaseMediaFunc = nullptr;
static mpv_handle* player = NULL;
static HANDLE hThread = NULL;
static bool stopThread = false;

OpenMedia GetOpenMedia() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (OpenMedia)((char*)hModule + 0x53790);
}

ReleaseMedia GetReleaseMedia() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (ReleaseMedia)((char*)hModule + 0x53480);
}

HWND* GetHwndPointer() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (HWND*)((char*)hModule + 0xf9848);
}

DWORD* GetPlayStatus() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (DWORD*)((char*)hModule + 0xf9844);
}

DWORD WINAPI ThreadHandle(LPVOID data) {
    while (1) {
        if (stopThread) {
            hThread = NULL;
            return 0;
        }
        mpv_event* event = mpv_wait_event(player, 0);
        if (event && event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            mpv_event_property* prop = (mpv_event_property*)event->data;
            if (strcmp(prop->name, "core-idle") == 0) {
                bool* idle = (bool*)prop->data;
                *GetPlayStatus() = *idle ? 1 : 0;
            }
        }
        Sleep(10);
    }
}

HRESULT __cdecl HookedOpenMedia(LPCCH video) {
    if (player) {
        mpv_terminate_destroy(player);
        player = NULL;
    }
    player = mpv_create();
    if (!player) {
        return E_FAIL;
    }
    int err = mpv_initialize(player);
    if (err) {
        return E_FAIL;
    }
    HWND hwnd = *GetHwndPointer();
    int64_t wid = (int64_t)(uint32_t)hwnd;
    mpv_set_option(player, "wid", MPV_FORMAT_INT64, &wid);
    mpv_set_option_string(player, "config", "no");
    mpv_set_option_string(player, "input-default-bindings", "no");
    mpv_set_option_string(player, "hwdec", "auto");
    mpv_set_option_string(player, "auto-window-resize", "no");
    mpv_set_option_string(player, "log-file", "mpv.log");
    const char* cmd[] = { "loadfile", video, nullptr };
    err = mpv_command(player, cmd);
    if (err) {
        return E_FAIL;
    }
    while (1) {
        mpv_event* event = mpv_wait_event(player, 10);
        if (event->event_id == MPV_EVENT_FILE_LOADED) {
            break;
        }
        if (event->event_id == MPV_EVENT_SHUTDOWN) {
            return E_FAIL;
        }
    }
    const char* play_cmd[] = { "set", "pause", "no", NULL };
    err = mpv_command(player, play_cmd);
    if (err) {
        return E_FAIL;
    }
    while (1) {
        int64_t idle = 0;
        if (mpv_get_property(player, "core-idle", MPV_FORMAT_FLAG, &idle) < 0) {
            return E_FAIL;
        }
        if (!idle) {
            break;
        }
        Sleep(10);
    }
    *GetPlayStatus() = 0;
    mpv_observe_property(player, 0, "core-idle", MPV_FORMAT_FLAG);
    stopThread = false;
    hThread = CreateThread(NULL, 0, ThreadHandle, NULL, 0, NULL);
    if (hThread == NULL) {
        mpv_terminate_destroy(player);
        player = NULL;
        return E_FAIL;
    }
    return S_OK;
}

LPVOID HookedReleaseVideo() {
    stopThread = true;
    while (hThread) {
        Sleep(10);
    }
    if (player) {
        mpv_terminate_destroy(player);
        player = NULL;
    }
    return NULL;
}

#endif

decltype(sprintf)* GetSprintf() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (decltype(sprintf)*)((char*)hModule + 0x5bd74);
}

DrawTextOn GetDrawTextOn() {
    HMODULE hModule = GetModuleHandleA(NULL);
    return (DrawTextOn)((char*)hModule + 0x36600);
}

int __cdecl HookedSprintf(char* buffer, const char* format, ...) {
    va_list args;
    if (!strcmp(format, "@i%s   ")) {
        va_list args;
        va_start(args, format);
        const char* str = va_arg(args, const char*);
        va_end(args);
        std::string s(str);
        auto find = stringsMap.find(s);
        if (find != stringsMap.end()) {
            s = find->second;
        }
        return sprintf(buffer, "@i%s", s.c_str());
    } else if (!strcmp(format, "%s  ")) {
        va_list args;
        va_start(args, format);
        const char* str = va_arg(args, const char*);
        va_end(args);
        std::string s(str);
        auto find = stringsMap.find(s);
        if (find != stringsMap.end()) {
            s = find->second;
        }
        return sprintf(buffer, "%s", s.c_str());
    }
    va_start(args, format);
    int ret = vsprintf(buffer, format, args);
    va_end(args);
    return ret;
}

int __fastcall HookedDrawTextOn(void* thisptr, void* edx, int a, int b, int c, const char* buffer) {
    if (!strncmp(buffer, "@i", 2)) {
        std::string s(buffer + 2);
        auto find = stringsMap.find(s);
        if (find != stringsMap.end()) {
            s = find->second;
        }
        s = "@i" + s;
        return DrawTextOnFunc(thisptr, a, b, c, s.c_str());
    }
    return DrawTextOnFunc(thisptr, a, b, c, buffer);
}

HFONT WINAPI HookedCreateFontIndirectA(CONST LOGFONTA* lplf) {
    if (lplf && lplf->lfFaceName) {
        UINT cp[] = { 932, CP_ACP };
        for (int i = 0; i < sizeof(cp); i++) {
            std::wstring tmp;
            if (wchar_util::str_to_wstr(tmp, lplf->lfFaceName, cp[i])) {
                LOGFONTW lf = { 0 };
                lf.lfHeight = lplf->lfHeight;
                lf.lfWidth = lplf->lfWidth;
                lf.lfEscapement = lplf->lfEscapement;
                lf.lfOrientation = lplf->lfOrientation;
                lf.lfWeight = lplf->lfWeight;
                lf.lfItalic = lplf->lfItalic;
                lf.lfUnderline = lplf->lfUnderline;
                lf.lfStrikeOut = lplf->lfStrikeOut;
                lf.lfCharSet = GB2312_CHARSET;
                lf.lfOutPrecision = lplf->lfOutPrecision;
                lf.lfClipPrecision = lplf->lfClipPrecision;
                lf.lfQuality = lplf->lfQuality;
                lf.lfPitchAndFamily = lplf->lfPitchAndFamily;
                if (tmp == L"ＭＳ ゴシック") {
                    tmp = L"黑体";
                    lf.lfWidth = 0;
                    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
                    if (lf.lfHeight == 22) {
                        //lf.lfHeight = 26;
                        lf.lfWeight = FW_BOLD;
                    } else if (lf.lfHeight == 16) {
                        //lf.lfHeight = 20;
                        lf.lfWeight = FW_BOLD;
                    } else if (lf.lfHeight == 18) {
                        //lf.lfHeight = 22;
                        lf.lfWeight = FW_BOLD;
                    } else if (lf.lfHeight == 20) {
                        //lf.lfHeight = 24;
                        lf.lfWeight = FW_BOLD;
                    }
                }
                wcscpy(lf.lfFaceName, tmp.c_str());
                return CreateFontIndirectW(&lf);
            }
        }
    }
    return TrueCreateFontIndirectA(lplf);
}

HANDLE WINAPI HookedCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    if (vfs.ContainsFile(lpFileName)) {
        return vfs.CreateFileW(lpFileName);
    }
    return TrueCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI HookedReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped) {
    if (vfs.ContainsHandle(hFile)) {
        if (lpOverlapped) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return vfs.ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead);
    }
    return TrueReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);
}

BOOL WINAPI HookedCloseHandle(HANDLE hObject) {
    if (vfs.ContainsHandle(hObject)) {
        vfs.CloseHandle(hObject);
        return TRUE;
    }
    return TrueCloseHandle(hObject);
}

DWORD WINAPI HookedGetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.GetFileSize(hFile, lpFileSizeHigh);
    }
    return TrueGetFileSize(hFile, lpFileSizeHigh);
}

BOOL WINAPI HookedGetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.GetFileSizeEx(hFile, lpFileSize);
    }
    return TrueGetFileSizeEx(hFile, lpFileSize);
}

DWORD WINAPI HookedSetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.SetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
    }
    return TrueSetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

BOOL WINAPI HookedSetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    if (vfs.ContainsHandle(hFile)) {
        return vfs.SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
    }
    return TrueSetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

DWORD WINAPI HookedGetFileType(HANDLE hFile) {
    if (vfs.ContainsHandle(hFile)) {
        return FILE_TYPE_DISK;
    }
    return TrueGetFileType(hFile);
}

DWORD WINAPI HookedGetFileAttributesW(LPCWSTR lpFileName) {
    if (vfs.ContainsFile(lpFileName)) {
        return FILE_ATTRIBUTE_READONLY;
    }
    return TrueGetFileAttributesW(lpFileName);
}

BOOL WINAPI HookedGetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    if (vfs.ContainsFile(lpFileName)) {
        return vfs.GetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
    }
    return TrueGetFileAttributesExW(lpFileName, fInfoLevelId, lpFileInformation);
}

BOOL WINAPI HookedMessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType) {
    UINT cp[] = { 932, CP_ACP };
    for (int i = 0; i < sizeof(cp); i++) {
        std::wstring wText, wCaption;
        if (wchar_util::str_to_wstr(wText, lpText, cp[i]) && wchar_util::str_to_wstr(wCaption, lpCaption, cp[i])) {
            auto f = dialogMap.find(wText);
            if (f != dialogMap.end()) {
                wText = f->second;
            }
            f = dialogMap.find(wCaption);
            if (f != dialogMap.end()) {
                wCaption = f->second;
            }
            for (const auto& re : dialogReList) {
                try {
                    wText = std::regex_replace(wText, re.first, re.second);
                } catch (...) {
                    // We don't care about error.
                }
            }
            return MessageBoxW(hWnd, wText.c_str(), wCaption.c_str(), uType);
        }
    }
    return TrueMessageBoxA(hWnd, lpText, lpCaption, uType);
}

HWND WINAPI HookedCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int x, int y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
    HWND hwnd = TrueCreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, x, y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
    if (hwnd && wTitle.length() > 0) {
        SetWindowTextW(hwnd, wTitle.c_str());
    }
    return hwnd;
}

HANDLE WINAPI HookedLoadImageA(HINSTANCE hInst, LPCSTR lpszName, UINT uType, int cxDesired, int cyDesired, UINT fuLoad) {
    return TrueLoadImageA(hInst, lpszName, uType, cxDesired, cyDesired, LR_LOADFROMFILE);
}

LSTATUS WINAPI HookedRegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions, REGSAM samDesired, PHKEY phkResult) {
    if (hKey == HKEY_CURRENT_USER && lpSubKey) {
        std::string value(lpSubKey);
        value = str_util::tolower(value);
        if (value == "software\\circus\\tmpl") {
            auto re = TrueRegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
            if (re == ERROR_SUCCESS) {
                phkSet.insert(*phkResult);
            }
            return re;
        }
    }
    return TrueRegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

LSTATUS WINAPI HookedRegCloseKey(HKEY hKey) {
    if (phkSet.find(hKey) != phkSet.end()) {
        phkSet.erase(hKey);
    }
    return TrueRegCloseKey(hKey);
}

LSTATUS WINAPI HookedRegQueryValueExA(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved, LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    if (phkSet.find(hKey) != phkSet.end()) {
        std::string value(lpValueName);
        value = str_util::tolower(value);
        if (value == "savedir" || value == "datadir") {
            return ERROR_FILE_NOT_FOUND;
        }
    }
    return TrueRegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

void Attach() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::string tmp;
    if (wchar_util::wstr_to_str(tmp, path, CP_UTF8)) {
        std::string fName = fileop::filename(tmp) + ".dat";
        vfs.AddArchive(fName);
    }
    vfs.AddArchive("video.dat");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
#if MPV
    OpenMediaFunc = GetOpenMedia();
    ReleaseMediaFunc = GetReleaseMedia();
    DetourAttach(&OpenMediaFunc, HookedOpenMedia);
    DetourAttach(&ReleaseMediaFunc, HookedReleaseVideo);
#endif
    sprintfFunc = GetSprintf();
    DrawTextOnFunc = GetDrawTextOn();
    DetourAttach(&sprintfFunc, HookedSprintf);
    DetourAttach(&(PVOID&)DrawTextOnFunc, HookedDrawTextOn);
    DetourAttach(&TrueCreateFontIndirectA, HookedCreateFontIndirectA);
    DetourAttach(&TrueCreateFileW, HookedCreateFileW);
    DetourAttach(&TrueReadFile, HookedReadFile);
    DetourAttach(&TrueCloseHandle, HookedCloseHandle);
    DetourAttach(&TrueGetFileSize, HookedGetFileSize);
    DetourAttach(&TrueGetFileSizeEx, HookedGetFileSizeEx);
    DetourAttach(&TrueSetFilePointer, HookedSetFilePointer);
    DetourAttach(&TrueSetFilePointerEx, HookedSetFilePointerEx);
    DetourAttach(&TrueGetFileType, HookedGetFileType);
    DetourAttach(&TrueGetFileAttributesW, HookedGetFileAttributesW);
    DetourAttach(&TrueGetFileAttributesExW, HookedGetFileAttributesExW);
    DetourAttach(&TrueMessageBoxA, HookedMessageBoxA);
    DetourAttach(&TrueCreateWindowExA, HookedCreateWindowExA);
    DetourAttach(&TrueLoadImageA, HookedLoadImageA);
    DetourAttach(&TrueRegOpenKeyExA, HookedRegOpenKeyExA);
    DetourAttach(&TrueRegCloseKey, HookedRegCloseKey);
    DetourAttach(&TrueRegQueryValueExA, HookedRegQueryValueExA);
    DetourTransactionCommit();
    HANDLE f = vfs.CreateFileW(L"title");
    if (f != INVALID_HANDLE_VALUE) {
        char buffer[256] = { 0 };
        DWORD bytesRead = 0;
        vfs.ReadFile(f, buffer, sizeof(buffer) - 1, &bytesRead);
        vfs.CloseHandle(f);
        std::string title(buffer, bytesRead);
        wchar_util::str_to_wstr(wTitle, title, CP_UTF8);
    }
    M3tFile dialog(L"dialog.m3t");
    if (!dialog.hasError) {
        for (const auto& msg : dialog.messages) {
            std::wstring ori, dst;
            if (msg.ori.empty() || (msg.dst.empty() && msg.llm.empty())) {
                continue;
            }
            auto& dst2 = msg.dst.empty() ? msg.llm : msg.dst;
            if (wchar_util::str_to_wstr(ori, msg.ori, CP_UTF8) && wchar_util::str_to_wstr(dst, dst2, CP_UTF8)) {
                if (ori.find(L"r\"") == 0 && ori.rfind(L"\"") == ori.size() - 1) {
                    ori = ori.substr(2, ori.size() - 3);
                    try {
                        std::wregex re(ori);
                        dialogReList.emplace_back(re, dst);
                    } catch (std::regex_error& e) {
                        std::wstring errMsg = L"Regex error: ";
                        std::wstring reMsg;
                        if (!wchar_util::str_to_wstr(reMsg, e.what(), CP_UTF8)) {
                            wchar_util::str_to_wstr(reMsg, e.what(), CP_ACP);
                        }
                        if (!reMsg.empty()) {
                            errMsg += reMsg;
                        }
                        errMsg += L"\n";
                        errMsg += ori;
                        MessageBoxW(NULL, errMsg.c_str(), L"Error", MB_OK | MB_ICONERROR);
                    }
                    continue;
                }
                dialogMap[ori] = dst;
            }
        }
    }
    StringsDat sDat(L"strings.dat");
    if (!sDat.hasError) {
        for (auto i = 0; i < sDat.strings.size(); i += 2) {
            stringsMap[sDat.strings[i]] = sDat.strings[i + 1];
        }
    }
}

void Detach() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
#if MPV
    DetourDetach(&OpenMediaFunc, HookedOpenMedia);
    DetourDetach(&ReleaseMediaFunc, HookedReleaseVideo);
#endif
    DetourDetach(&sprintfFunc, HookedSprintf);
    DetourDetach(&(PVOID&)DrawTextOnFunc, HookedDrawTextOn);
    DetourDetach(&TrueCreateFontIndirectA, HookedCreateFontIndirectA);
    DetourDetach(&TrueCreateFileW, HookedCreateFileW);
    DetourDetach(&TrueReadFile, HookedReadFile);
    DetourDetach(&TrueCloseHandle, HookedCloseHandle);
    DetourDetach(&TrueGetFileSize, HookedGetFileSize);
    DetourDetach(&TrueGetFileSizeEx, HookedGetFileSizeEx);
    DetourDetach(&TrueSetFilePointer, HookedSetFilePointer);
    DetourDetach(&TrueSetFilePointerEx, HookedSetFilePointerEx);
    DetourDetach(&TrueGetFileType, HookedGetFileType);
    DetourDetach(&TrueGetFileAttributesW, HookedGetFileAttributesW);
    DetourDetach(&TrueGetFileAttributesExW, HookedGetFileAttributesExW);
    DetourDetach(&TrueMessageBoxA, HookedMessageBoxA);
    DetourDetach(&TrueCreateWindowExA, HookedCreateWindowExA);
    DetourDetach(&TrueLoadImageA, HookedLoadImageA);
    DetourDetach(&TrueRegOpenKeyExA, HookedRegOpenKeyExA);
    DetourDetach(&TrueRegCloseKey, HookedRegCloseKey);
    DetourDetach(&TrueRegQueryValueExA, HookedRegQueryValueExA);
    DetourTransactionCommit();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID rev) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        Proxy::Init(hModule);
        Attach();
        break;
    case DLL_PROCESS_DETACH:
        Detach();
        break;
    }
    return TRUE;
}
