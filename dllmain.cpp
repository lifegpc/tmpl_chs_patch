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

static VFS vfs;
static std::wstring wTitle;
static std::unordered_map<std::wstring, std::wstring> dialogMap;
static std::vector<std::pair<std::wregex, std::wstring>> dialogReList;
static std::unordered_set<HKEY> phkSet;

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
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
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
                        MessageBoxW(NULL, L"Regex error", L"Error", MB_OK | MB_ICONERROR);
                    }
                    continue;
                }
                dialogMap[ori] = dst;
            }
        }
    }
}

void Detach() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
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
