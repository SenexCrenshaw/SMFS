// File: logger.cpp
#include "logger.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

std::ofstream Logger::g_logFile;
std::mutex Logger::g_logMutex;
LogLevel Logger::currentLogLevel = LogLevel::INFO; // Default to INFO
bool Logger::setDebug = false;

std::string ToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

std::string GetCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&nowTime), "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << nowMs.count();

    return oss.str();
}

void Logger::InitLogFile(const std::string &filePath)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open())
    {
        g_logFile.close();
    }
    g_logFile.open(filePath, std::ios::app);
    if (!g_logFile.good())
    {
        std::cerr << "[ERROR] Could not open log file: " << filePath << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string &msg)
{
    // Skip logs below the current log level
    if (level < currentLogLevel)
    {
        return;
    }
    if (setDebug)
    {
        std::cerr << "[" << ToString(level) << "] " << msg << std::endl;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);

    nlohmann::json logJson;
    logJson["level"] = ToString(level);
    logJson["timestamp"] = GetCurrentTimestamp();
    logJson["message"] = msg;

    if (g_logFile.is_open())
    {
        g_logFile << logJson.dump() << std::endl;
    }
}
