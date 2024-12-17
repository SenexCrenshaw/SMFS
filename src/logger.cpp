#include "logger.hpp"

// Define static members
std::mutex Logger::logMutex;
std::ofstream Logger::logFile;

// Initialize log file
void Logger::InitLogFile(const std::string &filePath)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logFile.open(filePath, std::ios::app);
    if (!logFile.is_open())
    {
        std::cerr << "Failed to open log file: " << filePath << std::endl;
    }
}

// Log a message
void Logger::Log(LogLevel level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(logMutex);
    std::string levelStr = LevelToString(level);

    std::ostringstream logStream;
    logStream << CurrentTime() << " [" << levelStr << "] " << message;

    // Print to console
    std::cout << logStream.str() << std::endl;

    // Write to file if open
    if (logFile.is_open())
    {
        logFile << logStream.str() << std::endl;
    }
}

// Convert LogLevel to string
std::string Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

// Get current time
std::string Logger::CurrentTime()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}
