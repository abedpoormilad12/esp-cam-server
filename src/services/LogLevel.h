// ============================================================
// LogLevel.h
// Log level definitions and severity ordering.
// ============================================================

#pragma once

#ifndef LOG_LEVEL_H
#define LOG_LEVEL_H

#include <cstddef>
#include <cstdint>

namespace Gateway {
namespace Services {

// ============================================================
// Log severity levels - ordered from lowest to highest
// ============================================================
enum class LogLevel : uint8_t {
    VERBOSE  = 0,
    DEBUG    = 1,
    INFO     = 2,
    WARNING  = 3,
    ERROR    = 4,
    CRITICAL = 5,
    NONE     = 6    // Disable all logging
};

// ============================================================
// Log entry - fixed size to avoid heap allocation
// Designed to fit in FreeRTOS queue without dynamic memory
// ============================================================
struct LogEntry {
    static constexpr size_t MAX_TAG_LENGTH     = 20;
    static constexpr size_t MAX_MESSAGE_LENGTH = 160;

    LogLevel  level;
    uint32_t  timestamp;                    // millis()
    uint32_t  taskId;                       // xTaskGetCurrentTaskHandle hash
    char      tag[MAX_TAG_LENGTH];
    char      message[MAX_MESSAGE_LENGTH];

    LogEntry()
        : level(LogLevel::NONE)
        , timestamp(0)
        , taskId(0)
        , tag{}
        , message{}
    {}
};

// Total size check — must fit comfortably in queue
static_assert(sizeof(LogEntry) <= 192, "LogEntry too large for queue");

// ============================================================
// Compile-time level label lookup
// ============================================================
inline constexpr const char* logLevelToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::VERBOSE:  return "VRB";
        case LogLevel::DEBUG:    return "DBG";
        case LogLevel::INFO:     return "INF";
        case LogLevel::WARNING:  return "WRN";
        case LogLevel::ERROR:    return "ERR";
        case LogLevel::CRITICAL: return "CRT";
        default:                 return "---";
    }
    return "UNKNOWN";
}

inline constexpr const char* logLevelToColor(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::VERBOSE:  return "\033[37m";     // White
        case LogLevel::DEBUG:    return "\033[36m";     // Cyan
        case LogLevel::INFO:     return "\033[32m";     // Green
        case LogLevel::WARNING:  return "\033[33m";     // Yellow
        case LogLevel::ERROR:    return "\033[31m";     // Red
        case LogLevel::CRITICAL: return "\033[1;31m";   // Bold Red
        default:                 return "\033[0m";
    }
}

inline constexpr const char* COLOR_RESET = "\033[0m";

} // namespace Services
} // namespace Gateway

#endif // LOG_LEVEL_H