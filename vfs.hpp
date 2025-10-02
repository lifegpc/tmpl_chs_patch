#include "zip.h"
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <Windows.h>
#include "str_util.h"

struct CaseInsensitiveHash {
    size_t operator()(const std::string& str) const {
        // 创建字符串的小写副本
        std::string lowercaseStr = str_util::tolower(str);
        
        // 对小写字符串使用标准哈希函数
        return std::hash<std::string>{}(lowercaseStr);
    }
};

// 比较函数，忽略大小写
struct CaseInsensitiveEqual {
    bool operator()(const std::string& left, const std::string& right) const {
        return left.size() == right.size() &&
               std::equal(left.begin(), left.end(), right.begin(),
                          [](unsigned char a, unsigned char b) {
                              return std::tolower(a) == std::tolower(b);
                          });
    }
};

class VFS {
    public:
        VFS();
        ~VFS();
        bool AddArchive(std::string path, bool inMem = false);
        bool AddArchiveFromResource(HMODULE hModule, LPCWSTR rcType, LPCWSTR rcName, bool inMem = false);
        bool AddArchiveFromResource(HMODULE hModule, int resourceID, bool inMem = false);
        void AddArchiveWithErrorMsg(std::string path, bool inMem = false);
        void AddArchiveFromResourceWithErrorMsg(HMODULE hModule, int resourceID, bool inMem = false);
        bool ContainsFile(std::string path);
        bool ContainsFile(std::wstring path);
        bool ContainsHandle(HANDLE hFile);
        HANDLE CreateFileW(std::wstring path);
        bool ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead);
        void CloseHandle(HANDLE hFile);
        BOOL GetFileAttributesExW(LPCWSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
        DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
        BOOL GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize);
        DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);
        BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod);
        std::unordered_map<std::string, zip_stat_t, CaseInsensitiveHash, CaseInsensitiveEqual> files;
        std::string GetBasePath();
    private:
        bool IsInMemArchive(zip_t* archive);
        bool IsInMemHandle(HANDLE hFile);
        std::string base_path;
        std::list<zip_t*> archives;
        std::unordered_set<zip_t*> inMemArchives;
        std::unordered_map<HANDLE, std::string> handles;
        std::unordered_map<HANDLE, std::string> inMemHandles;
};
