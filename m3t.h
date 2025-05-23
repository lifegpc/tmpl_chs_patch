#pragma once
#include <string>
#include <vector>

class M3tMessage {
public:
    M3tMessage(std::string ori, std::string name, std::string dst, std::string llm)
        : ori(ori), name(name), dst(dst), llm(llm) {
    }
    std::string ori;
    std::string name;
    std::string dst;
    std::string llm;
};

class M3tFile {
public:
    M3tFile(std::wstring path);
    std::vector<M3tMessage> messages;
    bool hasError = false;
};
