#pragma once
#include <string>
#include <fstream>
#include <mutex>

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

    // Set log level
    static void SetLogLevel(LogLevel level)
    {
        currentLogLevel = level;
    }

    static void SetDebug(bool debug)
    {
        setDebug = debug;
    }

private:
    static std::ofstream g_logFile;
    static std::mutex g_logMutex;
    static LogLevel currentLogLevel;
    static bool setDebug;
};
