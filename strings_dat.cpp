#include "strings_dat.h"
#include "file_reader.h"
#include "malloc.h"
#include <Windows.h>
#include "memfile.h"
#include <stdint.h>

StringsDat::StringsDat(std::wstring path) {
    HANDLE f = CreateFileW(path.c_str(), 0, 0, NULL, 0, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        hasError = true;
        return;
    }
    DWORD fileSize = GetFileSize(f, NULL);
    char* buf = new char[fileSize];
    DWORD bytesRead = 0;
    if (!ReadFile(f, buf, fileSize, &bytesRead, NULL)) {
        hasError = true;
        delete[] buf;
        CloseHandle(f);
        return;
    }
    CloseHandle(f);
    auto mem = new_cmemfile(buf, bytesRead);
    auto reader = create_file_reader2(mem, cmemfile_read2, cmemfile_seek2, cmemfile_tell2, 0);
    uint16_t stringSize;
    while (!file_reader_read_uint16(reader, &stringSize)) {
        strings.push_back(std::string(buf + file_reader_tell(reader), stringSize));
        file_reader_seek(reader, stringSize, SEEK_CUR);
    }
    free_file_reader(reader);
    free_cmemfile(mem);
    delete[] buf;
}

