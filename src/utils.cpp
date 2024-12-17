#include "utils.hpp"
#include <algorithm>
#include <cctype>

// Function to check if a string ends with a particular suffix
bool ends_with(const std::string &value, const std::string &ending)
{
    if (ending.size() > value.size())
        return false;

    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

// Function to trim leading and trailing whitespace from a string
std::string trim(const std::string &str)
{
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start))
        start++;

    auto end = str.end();
    do
    {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}
