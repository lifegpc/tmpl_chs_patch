#pragma once
#include <string>
#include <vector>

class StringsDat {
public:
    StringsDat(std::wstring path);
    std::vector<std::string> strings;
    bool hasError = false;
};
