#include "logger.hpp"
#include <fstream>
#include <iostream>
#include <mutex>

static std::mutex g_logMutex;
static std::ofstream g_logFile;

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
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (!g_logFile.is_open())
    {
        // fallback to stderr
        std::cerr << "[LOGGER] " << msg << std::endl;
        return;
    }
    std::cout << "[LOGGER] " << msg << std::endl;
    switch (level)
    {
    case LogLevel::DEBUG:
        g_logFile << "[DEBUG] ";
        break;
    case LogLevel::INFO:
        g_logFile << "[INFO ] ";
        break;
    case LogLevel::WARN:
        g_logFile << "[WARN ] ";
        break;
    case LogLevel::ERROR:
        g_logFile << "[ERROR] ";
        break;
    }
    g_logFile << msg << std::endl;
    g_logFile.flush();
}
