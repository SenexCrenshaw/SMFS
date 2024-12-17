#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <string>
#include <chrono>
#include <iomanip>

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
    static void Log(LogLevel level, const std::string &message);

private:
    static std::string LevelToString(LogLevel level);
    static std::string CurrentTime();

    static std::mutex logMutex;
    static std::ofstream logFile;
};
