#pragma once

#include "nlohmann/json.hpp"
#include <map>
#include <vector>
#include <string>
#include <mutex>

using json = nlohmann::json;

// Structure to represent a virtual file
struct VirtualFile
{
    std::string name;    // File name (e.g., "Furiosa: A Mad Max Saga 2024.strm")
    std::string content; // File content (e.g., URL for .strm files or dummy placeholder)
};

// Virtual filesystem state
class VirtualFS
{
public:
    void LoadFromJson(const json &data); // Load JSON data into the virtual filesystem

    std::map<std::string, std::vector<VirtualFile>> directories; // Map: path -> files
    std::mutex fsMutex;                                          // Protect the directories map

private:
    std::string StripPrefixes(const std::string &name); // Helper function to clean prefixes
};
