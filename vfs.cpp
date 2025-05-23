#include "vfs.hpp"
#include "wchar_util.h"
#include "str_util.h"
#include "fileop.h"
#include "shlwapi.h"
#include "time_util.h"
#include "memfile.h"

DWORD mapZipError(zip_file_t* file) {
    auto error = zip_file_get_error(file);
    if (error) {
        switch (error->zip_err) {
        case ZIP_ER_EOF:
            return ERROR_HANDLE_EOF;
        case ZIP_ER_INVAL:
            return ERROR_INVALID_PARAMETER;
        case ZIP_ER_SEEK:
            return ERROR_SEEK;
        case ZIP_ER_READ:
            return ERROR_READ_FAULT;
        case ZIP_ER_CRC:
            return ERROR_CRC;
        case ZIP_ER_ZIPCLOSED:
            return ERROR_INVALID_HANDLE;
        case ZIP_ER_NOENT:
            return ERROR_FILE_NOT_FOUND;
        case ZIP_ER_EXISTS:
            return ERROR_FILE_EXISTS;
        case ZIP_ER_OPEN:
            return ERROR_OPEN_FAILED;
        }
    }
    return ERROR_INVALID_HANDLE;
}

VFS::VFS() {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path = exePath;
    std::string pathStr;
    if (!wchar_util::wstr_to_str(pathStr, path, CP_UTF8)) {
        char buf[MAX_PATH];
        GetModuleFileNameA(NULL, buf, MAX_PATH);
        pathStr = buf;
    }
    base_path = fileop::dirname(pathStr);
    base_path = str_util::str_replace(base_path, "/", "\\");
}

VFS::~VFS() {
    for (auto file : handles) {
        zip_fclose((zip_file_t*)file.first);
    }
    for (auto archive : archives) {
        zip_close(archive);
    }
}

bool VFS::AddArchive(std::string path, bool inMem) {
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, nullptr);
    if (!archive) return false;
    archives.push_front(archive);
    auto len = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < len; i++) {
        struct zip_stat st;
        zip_stat_init(&st);
        zip_stat_index(archive, i, 0, &st);
        // Skip directories/folders (directory entries usually end with a '/')
        if (st.name[strlen(st.name) - 1] == '/') {
            continue;
        }
        std::string name = st.name;
        name = str_util::str_replace(name, "/", "\\");
        files[name] = st;
    }
    if (inMem) {
        inMemArchives.insert(archive);
    }
    return true;
}

bool VFS::AddArchiveFromResource(HMODULE hModule, int resourceID, bool inMem) {
    HRSRC hResInfo = FindResource(hModule, MAKEINTRESOURCE(resourceID), RT_RCDATA);
    if (!hResInfo) return false;
    HGLOBAL hResData = LoadResource(hModule, hResInfo);
    if (!hResData) return false;
    LPVOID lpResData = LockResource(hResData);
    if (!lpResData) return false;
    DWORD dwSize = SizeofResource(hModule, hResInfo);
    if (!dwSize) return false;
    auto re = zip_source_buffer_create(lpResData, dwSize, 0, nullptr);
    if (!re) {
        return false;
    }
    zip_t* archive = zip_open_from_source(re, ZIP_RDONLY, nullptr);
    if (!archive) return false;
    archives.push_front(archive);
    auto len = zip_get_num_entries(archive, 0);
    for (zip_int64_t i = 0; i < len; i++) {
        struct zip_stat st;
        zip_stat_init(&st);
        zip_stat_index(archive, i, 0, &st);
        // Skip directories/folders (directory entries usually end with a '/')
        if (st.name[strlen(st.name) - 1] == '/') {
            continue;
        }
        std::string name = st.name;
        name = str_util::str_replace(name, "/", "\\");
        files[name] = st;
    }
    if (inMem) {
        inMemArchives.insert(archive);
    }
    return true;
}

void VFS::AddArchiveWithErrorMsg(std::string path, bool inMem) {
    if (!AddArchive(path, inMem)) {
        std::wstring wpath;
        if (!wchar_util::str_to_wstr(wpath, path, CP_UTF8)) {
            MessageBoxW(NULL, L"无法打开资源文件。请检查资源文件是否完整", L"错误", MB_ICONERROR);
            ExitProcess(1);
            return;
        }
        std::wstring wmsg = L"无法打开 " + wpath + L"。请检查文件是否存在";
        MessageBoxW(NULL, wmsg.c_str(), L"错误", MB_ICONERROR);
        ExitProcess(1);
        return;
    }
}

void VFS::AddArchiveFromResourceWithErrorMsg(HMODULE hModule, int resourceID, bool inMem) {
    if (!AddArchiveFromResource(hModule, resourceID, inMem)) {
        MessageBoxW(NULL, L"无法打开内置的资源文件。", L"错误", MB_ICONERROR);
        ExitProcess(1);
        return;
    }
}

bool VFS::ContainsFile(std::string path) {
    path = str_util::str_replace(path, "/", "\\");
    if (fileop::isabs(path)) {
        path = fileop::relpath(path, base_path);
    }
    return files.find(path) != files.end();
}

bool VFS::ContainsFile(std::wstring path) {
    std::string str;
    if (!wchar_util::wstr_to_str(str, path, CP_UTF8)) {
        return false;
    }
    return ContainsFile(str);
}

bool VFS::ContainsHandle(HANDLE hFile) {
    return handles.find(hFile) != handles.end() || IsInMemHandle(hFile);
}

HANDLE VFS::CreateFileW(std::wstring path) {
    std::string str;
    if (!wchar_util::wstr_to_str(str, path, CP_UTF8)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    str = fileop::relpath(str, base_path);
    str = str_util::str_replace(str, "/", "\\");
    auto c = files.find(str);
    if (c == files.end()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    str = (*c).first;
    str = str_util::str_replace(str, "\\", "/");
    zip_t* archive = nullptr;
    zip_uint64_t index = 0;
    for (auto a : archives) {
        if (zip_name_locate(a, str.c_str(), 0) != -1) {
            archive = a;
            break;
        }
    }
    if (!archive) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    index = zip_name_locate(archive, str.c_str(), 0);
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (IsInMemArchive(archive)) {
        auto& stat = (*c).second;
        auto size = stat.size;
        char* mem = new char[size];
        zip_int64_t n = zip_fread(file, mem, size);
        if (n == -1) {
            SetLastError(mapZipError(file));
            zip_fclose(file);
            return INVALID_HANDLE_VALUE;
        }
        zip_fclose(file);
        auto memfile = new_memfile(mem, size);
        delete[] mem;
        if (!memfile) {
            SetLastError(ERROR_OUTOFMEMORY);
            return INVALID_HANDLE_VALUE;
        }
        inMemHandles[(HANDLE)memfile] = str;
        return (HANDLE)memfile;
    } else {
        handles[(HANDLE)file] = str;
    }
    return (HANDLE)file;
}

bool VFS::ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return false;
    }
    if (IsInMemHandle(hFile)) {
        MemFile* file = (MemFile*)hFile;
        auto readed = memfile_read(file, (char*)lpBuffer, nNumberOfBytesToRead);
        if (readed == 0) {
            SetLastError(ERROR_HANDLE_EOF);
            return false;
        }
        if (readed == -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }
        if (lpNumberOfBytesRead) {
            *lpNumberOfBytesRead = readed;
        }
        return true;
    }
    zip_file_t* file = (zip_file_t*)hFile;
    zip_int64_t n = zip_fread(file, lpBuffer, nNumberOfBytesToRead);
    if (n == -1) {
        SetLastError(mapZipError(file));
        return false;
    }
    if (lpNumberOfBytesRead) {
        *lpNumberOfBytesRead = n;
    }
    return true;
}

void VFS::CloseHandle(HANDLE hFile) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return;
    }
    if (IsInMemHandle(hFile)) {
        MemFile* file = (MemFile*)hFile;
        free_memfile(file);
        inMemHandles.erase(hFile);
        return;
    }
    zip_fclose((zip_file_t*)hFile);
    handles.erase(hFile);
}

DWORD VFS::GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
    auto f = handles.find(hFile);
    if (f == handles.end()) {
        f = inMemHandles.find(hFile);
        if (f == inMemHandles.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return INVALID_FILE_SIZE;
        }
    }
    auto data = *f;
    auto name = data.second;
    auto size = files[name].size;
    if (lpFileSizeHigh) {
        *lpFileSizeHigh = size >> 32;
    }
    return size;
}

BOOL VFS::GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    auto f = handles.find(hFile);
    if (f == handles.end()) {
        f = inMemHandles.find(hFile);
        if (f == inMemHandles.end()) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
    }
    auto data = *f;
    auto name = data.second;
    auto size = files[name].size;
    lpFileSize->QuadPart = size;
    return TRUE;
}

DWORD VFS::SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    if (IsInMemHandle(hFile)) {
        MemFile* file = (MemFile*)hFile;
        if (!file) {
            SetLastError(ERROR_INVALID_HANDLE);
            return INVALID_SET_FILE_POINTER;
        }
        int64_t offset = lDistanceToMove;
        if (lpDistanceToMoveHigh) {
            offset |= ((int64_t)*lpDistanceToMoveHigh) << 32;
        }
        int code = memfile_seek(file, offset, dwMoveMethod);
        if (code == -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }
        int64_t n = memfile_tell(file);
        if (n == -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return INVALID_SET_FILE_POINTER;
        }
        if (lpDistanceToMoveHigh) {
            *lpDistanceToMoveHigh = n >> 32;
        }
        return n;
    }
    zip_file_t* file = (zip_file_t*)hFile;
    if (!file) {
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    zip_int64_t offset = lDistanceToMove;
    if (lpDistanceToMoveHigh) {
        offset |= ((zip_int64_t)*lpDistanceToMoveHigh) << 32;
    }
    zip_int8_t code = zip_fseek(file, offset, dwMoveMethod);
    if (code == -1) {
        SetLastError(mapZipError(file));
        return INVALID_SET_FILE_POINTER;
    }
    zip_int64_t n = zip_ftell(file);
    if (n == -1) {
        SetLastError(mapZipError(file));
        return INVALID_SET_FILE_POINTER;
    }
    if (lpDistanceToMoveHigh) {
        *lpDistanceToMoveHigh = n >> 32;
    }
    return n;
}

BOOL VFS::SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    if (!ContainsHandle(hFile)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (IsInMemHandle(hFile)) {
        MemFile* file = (MemFile*)hFile;
        if (!file) {
            SetLastError(ERROR_INVALID_HANDLE);
            return FALSE;
        }
        zip_int64_t offset = liDistanceToMove.QuadPart;
        int code = memfile_seek(file, offset, dwMoveMethod);
        if (code == -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        zip_int64_t n = memfile_tell(file);
        if (n == -1) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (lpNewFilePointer) {
            lpNewFilePointer->QuadPart = n;
        }
        return TRUE;
    }
    zip_file_t* file = (zip_file_t*)hFile;
    if (!file) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    zip_int64_t offset = liDistanceToMove.QuadPart;
    zip_int8_t code = zip_fseek(file, offset, dwMoveMethod);
    if (code == -1) {
        SetLastError(mapZipError(file));
        return FALSE;
    }
    zip_int64_t n = zip_ftell(file);
    if (n == -1) {
        SetLastError(mapZipError(file));
        return FALSE;
    }
    if (lpNewFilePointer) {
        lpNewFilePointer->QuadPart = n;
    }
    return TRUE;
}

std::string VFS::GetBasePath() {
    return base_path;
}

BOOL VFS::GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation) {
    std::string path;
    if (!wchar_util::wstr_to_str(path, lpFileName, CP_UTF8)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    path = str_util::str_replace(path, "/", "\\");
    if (fileop::isabs(path)) {
        path = fileop::relpath(path, base_path);
    }
    auto c = files.find(path);
    if (c == files.end()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    auto st = (*c).second;
    if (fInfoLevelId == GetFileExInfoStandard) {
        if (!lpFileInformation) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        WIN32_FILE_ATTRIBUTE_DATA data;
        data.dwFileAttributes = FILE_ATTRIBUTE_READONLY;
        time_util::time_t_to_file_time(st.mtime, &data.ftLastWriteTime);
        data.ftCreationTime = data.ftLastWriteTime;
        data.ftLastAccessTime = data.ftLastWriteTime;
        data.nFileSizeHigh = st.size >> 32;
        data.nFileSizeLow = st.size & 0xFFFFFFFF;
        memcpy(lpFileInformation, &data, sizeof(data));
        return TRUE;
    }
    return FALSE;
}

bool VFS::IsInMemArchive(zip_t* archive) {
    return inMemArchives.find(archive) != inMemArchives.end();
}

bool VFS::IsInMemHandle(HANDLE hFile) {
    return inMemHandles.find(hFile) != inMemHandles.end();
}
