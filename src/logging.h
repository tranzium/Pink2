// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Phase 6C: Lightweight structured logging with levels and categories.
// Routes through existing OutputDebugStringF infrastructure for backward
// compatibility — timestamps, file/console output, thread safety preserved.

#ifndef BITCOIN_LOGGING_H
#define BITCOIN_LOGGING_H

#include <atomic>
#include <cstdint>
#include <string>

enum class LogLevel : int {
    NONE  = 0,
    ERROR = 1,
    WARN  = 2,
    INFO  = 3,
    DEBUG = 4
};

namespace BCLog {
enum Category : uint32_t {
    NONE      = 0,
    NET       = (1u << 0),
    WALLET    = (1u << 1),
    STAKE     = (1u << 2),
    RPC       = (1u << 3),
    CONSENSUS = (1u << 4),
    SMSG      = (1u << 5),
    MEMPOOL   = (1u << 6),
    DB        = (1u << 7),
    ALL       = 0xFFFFFFFFu
};
} // namespace BCLog

class Logger {
    std::atomic<int> m_level{static_cast<int>(LogLevel::INFO)};
    std::atomic<uint32_t> m_categories{0};

public:
    static Logger& GetInstance();

    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;

    void EnableCategory(BCLog::Category cat);
    void DisableCategory(BCLog::Category cat);
    void SetCategories(uint32_t cats);
    uint32_t GetCategories() const;

    bool WillLog(LogLevel level) const;
    bool WillLog(LogLevel level, BCLog::Category cat) const;

    // Format and write a log line (thread-safe, routes through OutputDebugStringF)
    void LogPrintStr(const std::string& str);

    // String conversion helpers
    static BCLog::Category CategoryFromString(const std::string& name);
    static std::string CategoryToString(BCLog::Category cat);
    static LogLevel LevelFromString(const std::string& name);
    static std::string LevelToString(LogLevel level);
};

// Convenience macros — call real_strprintf directly with ##__VA_ARGS__
// to handle zero-arg case (e.g., LogPrintf("message\n") with no format args).
// real_strprintf is declared in util.h; macros expand lazily at point of use.

// LogPrintf: logs at INFO level, no category filter
#define LogPrintf(format, ...) do { \
    if (Logger::GetInstance().WillLog(LogLevel::INFO)) { \
        Logger::GetInstance().LogPrintStr(real_strprintf(format, 0, ##__VA_ARGS__)); \
    } \
} while(0)

// LogPrint: logs at DEBUG level, filtered by category
#define LogPrint(category, format, ...) do { \
    if (Logger::GetInstance().WillLog(LogLevel::DEBUG, category)) { \
        Logger::GetInstance().LogPrintStr(real_strprintf(format, 0, ##__VA_ARGS__)); \
    } \
} while(0)

// LogError: always logs at ERROR level (unless level is NONE)
#define LogError(format, ...) do { \
    if (Logger::GetInstance().WillLog(LogLevel::ERROR)) { \
        Logger::GetInstance().LogPrintStr("ERROR: " + real_strprintf(format, 0, ##__VA_ARGS__)); \
    } \
} while(0)

#endif // BITCOIN_LOGGING_H
