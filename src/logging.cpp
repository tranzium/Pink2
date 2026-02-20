// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "logging.h"
#include "util.h"
#include "string_utils.h"

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

void Logger::SetLogLevel(LogLevel level)
{
    m_level.store(static_cast<int>(level));
}

LogLevel Logger::GetLogLevel() const
{
    return static_cast<LogLevel>(m_level.load());
}

void Logger::EnableCategory(BCLog::Category cat)
{
    m_categories.fetch_or(static_cast<uint32_t>(cat));
}

void Logger::DisableCategory(BCLog::Category cat)
{
    m_categories.fetch_and(~static_cast<uint32_t>(cat));
}

void Logger::SetCategories(uint32_t cats)
{
    m_categories.store(cats);
}

uint32_t Logger::GetCategories() const
{
    return m_categories.load();
}

bool Logger::WillLog(LogLevel level) const
{
    return static_cast<int>(level) <= m_level.load();
}

bool Logger::WillLog(LogLevel level, BCLog::Category cat) const
{
    if (static_cast<int>(level) > m_level.load())
        return false;
    return (m_categories.load() & static_cast<uint32_t>(cat)) != 0;
}

void Logger::LogPrintStr(const std::string& str)
{
    // Route through existing OutputDebugStringF which handles:
    // - file vs console vs debugger output
    // - timestamps (fLogTimestamps)
    // - thread safety (mutexDebugLog)
    // - log file reopening (fReopenDebugLog)
    OutputDebugStringF("%s", str.c_str());
}

BCLog::Category Logger::CategoryFromString(const std::string& name)
{
    std::string lower = strutil::to_lower_copy(name);
    if (lower == "net")       return BCLog::NET;
    if (lower == "wallet")    return BCLog::WALLET;
    if (lower == "stake")     return BCLog::STAKE;
    if (lower == "rpc")       return BCLog::RPC;
    if (lower == "consensus") return BCLog::CONSENSUS;
    if (lower == "smsg")      return BCLog::SMSG;
    if (lower == "mempool")   return BCLog::MEMPOOL;
    if (lower == "db")        return BCLog::DB;
    if (lower == "all" || lower == "1") return BCLog::ALL;
    return BCLog::NONE;
}

std::string Logger::CategoryToString(BCLog::Category cat)
{
    switch (cat) {
        case BCLog::NET:       return "net";
        case BCLog::WALLET:    return "wallet";
        case BCLog::STAKE:     return "stake";
        case BCLog::RPC:       return "rpc";
        case BCLog::CONSENSUS: return "consensus";
        case BCLog::SMSG:      return "smsg";
        case BCLog::MEMPOOL:   return "mempool";
        case BCLog::DB:        return "db";
        case BCLog::ALL:       return "all";
        default:               return "none";
    }
}

LogLevel Logger::LevelFromString(const std::string& name)
{
    std::string lower = strutil::to_lower_copy(name);
    if (lower == "error") return LogLevel::ERR;
    if (lower == "warn")  return LogLevel::WARN;
    if (lower == "info")  return LogLevel::INFO;
    if (lower == "debug") return LogLevel::DEBUG;
    if (lower == "none")  return LogLevel::NONE;
    return LogLevel::INFO; // default
}

std::string Logger::LevelToString(LogLevel level)
{
    switch (level) {
        case LogLevel::NONE:  return "none";
        case LogLevel::ERR: return "error";
        case LogLevel::WARN:  return "warn";
        case LogLevel::INFO:  return "info";
        case LogLevel::DEBUG: return "debug";
        default:              return "info";
    }
}
