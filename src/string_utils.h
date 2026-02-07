// Copyright (c) 2024 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PINKCOIN_STRING_UTILS_H
#define PINKCOIN_STRING_UTILS_H

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace strutil {

/**
 * Trim whitespace from the left side of a string (in-place)
 */
inline std::string& ltrim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return s;
}

/**
 * Trim whitespace from the right side of a string (in-place)
 */
inline std::string& rtrim(std::string& s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
    return s;
}

/**
 * Trim whitespace from both sides of a string (in-place)
 */
inline std::string& trim(std::string& s) {
    return ltrim(rtrim(s));
}

/**
 * Return a trimmed copy of the string
 */
inline std::string trim_copy(const std::string& s) {
    std::string copy = s;
    return trim(copy);
}

/**
 * Convert string to lowercase (in-place)
 */
inline std::string& to_lower(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

/**
 * Return a lowercase copy of the string
 */
inline std::string to_lower_copy(const std::string& s) {
    std::string copy = s;
    return to_lower(copy);
}

/**
 * Split a string by any character in delimiters
 */
inline std::vector<std::string> split(const std::string& s, const std::string& delimiters) {
    std::vector<std::string> result;
    std::size_t start = 0;
    std::size_t end = s.find_first_of(delimiters);

    while (end != std::string::npos) {
        result.push_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find_first_of(delimiters, start);
    }
    result.push_back(s.substr(start));
    return result;
}

/**
 * Split a string by a single delimiter character
 */
inline std::vector<std::string> split(const std::string& s, char delimiter) {
    return split(s, std::string(1, delimiter));
}

/**
 * Replace all occurrences of 'from' with 'to' in string (in-place)
 */
inline std::string& replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

/**
 * Check if string starts with prefix
 */
inline bool starts_with(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) return false;
    return s.compare(0, prefix.size(), prefix) == 0;
}

/**
 * Check if string ends with suffix
 */
inline bool ends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/**
 * Case-insensitive check if string starts with prefix
 */
inline bool istarts_with(const std::string& s, const std::string& prefix) {
    if (prefix.size() > s.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), s.begin(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
}

/**
 * Case-insensitive check if string ends with suffix
 */
inline bool iends_with(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
        [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
}

/**
 * Join a vector of strings with a delimiter
 */
inline std::string join(const std::vector<std::string>& v, const std::string& delimiter) {
    if (v.empty()) return "";
    std::string result = v[0];
    for (std::size_t i = 1; i < v.size(); ++i) {
        result += delimiter;
        result += v[i];
    }
    return result;
}

/**
 * Parse a config file (key=value format) from an input stream.
 * Calls the provided callback for each key-value pair found.
 *
 * Format:
 *   - Lines starting with # are comments
 *   - Empty lines are skipped
 *   - Format: key=value (whitespace around = is trimmed)
 *   - Multiple entries with same key are allowed
 */
template<typename Callback>
inline void parse_config_file(std::istream& stream, Callback callback) {
    std::string line;
    while (std::getline(stream, line)) {
        // Trim the line
        trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // Find the = separator
        std::size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos)
            continue;

        // Extract key and value
        std::string key = line.substr(0, eq_pos);
        std::string value = line.substr(eq_pos + 1);

        // Trim key and value
        trim(key);
        trim(value);

        // Skip if key is empty
        if (key.empty())
            continue;

        // Call the callback with key and value
        callback(key, value);
    }
}

} // namespace strutil

#endif // PINKCOIN_STRING_UTILS_H
