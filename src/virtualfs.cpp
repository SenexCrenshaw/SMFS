#include "virtualfs.hpp"

std::string VirtualFS::StripPrefixes(const std::string &name)
{
    static const std::vector<std::string> prefixes = {"HD :", "VOD:", "SD :"};
    std::string cleanedName = name;

    for (const auto &prefix : prefixes)
    {
        if (cleanedName.find(prefix) == 0) // Starts with prefix
        {
            cleanedName = cleanedName.substr(prefix.length()); // Remove prefix
            break;
        }
    }
    return cleanedName;
}

void VirtualFS::LoadFromJson(const json &data)
{
    std::lock_guard<std::mutex> lock(fsMutex);
    directories.clear();

    for (auto &entry : data["dirSMFSFiles"].items())
    {
        std::string dirName = entry.key(); // e.g., "Test"

        for (auto &file : entry.value())
        {
            std::string rawName = file["name"];
            std::string cleanedName = StripPrefixes(rawName); // Clean file name
            std::string url = file["url"];

            // Full directory path (e.g., "/Test/Furiosa: A Mad Max Saga 2024")
            std::string fullDirPath = "/" + dirName + "/" + cleanedName;

            // Add two virtual files to the directory
            directories[fullDirPath] = {
                {cleanedName + ".strm", url},                       // Streaming file with URL
                {cleanedName + ".ts", "This is a placeholder file"} // Dummy content for .ts file
            };
        }
    }
}
