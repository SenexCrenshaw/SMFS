// File: sgfs.hpp
#pragma once
#include <string>
#include <vector>

class SMFile
{
public:
    std::string name;
    std::string url;

    SMFile() = default;
    SMFile(const std::string &n, const std::string &u)
        : name(n), url(u) {}
};

class SGFS
{
public:
    std::string name;
    std::string url;
    std::vector<SMFile> smFiles;

    SGFS() = default;
    SGFS(const std::string &n, const std::string &u)
        : name(n), url(u) {}

    void addSMFile(const SMFile &file)
    {
        smFiles.push_back(file);
    }
};
