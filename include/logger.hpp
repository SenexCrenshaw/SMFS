#pragma once
#include <string>

enum class LogLevel
{
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger
{
public:
    static void InitLogFile(const std::string &filePath);
    static void Log(LogLevel level, const std::string &msg);
};
