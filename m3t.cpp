#include "m3t.h"
#include "file_reader.h"
#include "malloc.h"
#include "str_util.h"
#include <Windows.h>
#include "memfile.h"

using namespace str_util;

M3tFile::M3tFile(std::wstring path) {
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
    char* line = nullptr;
    size_t line_size = 0;
    std::string name, ori, dst, llm;
    while (!file_reader_read_line(reader, &line, &line_size)) {
        std::string l(line, line_size);
        free(line);
        l = str_trim(l);
        if (l.find("○") == 0) {
            l = str_trim(l.substr(3));
            if (l.find("NAME:") == 0) {
                name = str_replace(str_trim(l.substr(5)), "\\n", "\n");
            }  else {
                ori = str_replace(str_trim(l), "\\n", "\n");
            }
        } else if (l.find("△") == 0) {
            llm = str_replace(str_trim(l.substr(3)), "\\n", "\n");
        } else if (l.find("●") == 0) {
            dst = str_replace(str_trim(l.substr(3)), "\\n", "\n");
            messages.push_back(M3tMessage(ori, name, dst, llm));
            ori = "";
            name = "";
            dst = "";
            llm = "";
        }
    }
    free_file_reader(reader);
    free_cmemfile(mem);
    delete[] buf;
}
