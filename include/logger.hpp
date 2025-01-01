#pragma once
#include <string>

enum class LogLevel
{
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger
{
public:
    static void InitLogFile(const std::string &filePath);
    static void Log(LogLevel level, const std::string &msg);
};
