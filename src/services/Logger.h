// ============================================================
// Logger.h
// Production-grade asynchronous logger for ESP32.
//
// Design decisions:
//   - Non-blocking: callers post to a FreeRTOS queue
//   - Fixed-size LogEntry structs: zero heap allocation
//   - Dedicated low-priority task drains the queue
//   - Multiple output backends (Serial, File - future)
//   - Thread-safe by design
//   - Compile-time level filtering to reduce flash usage
//   - printf-style formatting with snprintf (no std::string)
// ============================================================

#pragma once

#ifndef LOGGER_H
#define LOGGER_H

#include "LogLevel.h"
#include "../interfaces/IService.h"
#include "../interfaces/IHealthCheck.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>

namespace Gateway {
namespace Services {

using Interfaces::ServiceState;

// ============================================================
// ILogBackend
// Abstract backend so we can add File/Network sinks later
// without changing Logger internals.
// ============================================================
class ILogBackend {
public:
    virtual ~ILogBackend() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() {}
    [[nodiscard]] virtual const char* getName() const = 0;
};

// ============================================================
// Logger
// Singleton service — only one instance in the system.
// ============================================================
class Logger final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    // Maximum number of pluggable backends
    static constexpr uint8_t MAX_BACKENDS = 3;

    // --------------------------------------------------------
    // Singleton access
    // --------------------------------------------------------
    [[nodiscard]] static Logger& getInstance() noexcept;

    // --------------------------------------------------------
    // IService implementation
    // --------------------------------------------------------
    [[nodiscard]] Result           initialize()        override;
    [[nodiscard]] Result           start()             override;
    [[nodiscard]] Result           stop()              override;
    [[nodiscard]] ServiceState     getState()   const  override;
    [[nodiscard]] const char*      getName()    const  override { return "Logger"; }
    [[nodiscard]] bool             isHealthy()  const  override;

    // --------------------------------------------------------
    // IHealthCheck implementation
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "Logger"; }

    // --------------------------------------------------------
    // Backend management
    // --------------------------------------------------------
    [[nodiscard]] Result addBackend(ILogBackend* backend);

    // --------------------------------------------------------
    // Runtime level control
    // --------------------------------------------------------
    void setLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel getLevel() const noexcept;

    // --------------------------------------------------------
    // Core logging API
    // All methods are non-blocking — they post to queue.
    // If queue is full, entry is silently dropped.
    // --------------------------------------------------------
    void log(LogLevel level, const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));

    void verbose(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    void debug(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    void info(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    void warning(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    void error(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    void critical(const char* tag, const char* fmt, ...)
        __attribute__((format(printf, 3, 4)));

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------
    struct Stats {
        uint32_t totalEnqueued;
        uint32_t totalDropped;
        uint32_t totalWritten;
        uint32_t queueHighWaterMark;
    };
    [[nodiscard]] Stats getStats() const noexcept;

    // Flush all pending entries synchronously
    void flush(uint32_t timeoutMs = 2000);

private:
    // --------------------------------------------------------
    // Private constructor — singleton
    // --------------------------------------------------------
    Logger();
    ~Logger();

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&)                 = delete;
    Logger& operator=(Logger&&)      = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    void logInternal(LogLevel level, const char* tag,
                     const char* fmt, va_list args);

    void dispatchToBackends(const LogEntry& entry);

    // --------------------------------------------------------
    // FreeRTOS task entry point
    // --------------------------------------------------------
    static void loggerTaskEntry(void* param);
    void        loggerTask();

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    ServiceState            m_state;
    std::atomic<uint8_t>    m_level;

    // FreeRTOS handles
    QueueHandle_t           m_queue;
    TaskHandle_t            m_taskHandle;
    SemaphoreHandle_t       m_backendMutex;

    // Queue storage — statically allocated
    static constexpr uint8_t QUEUE_LENGTH = Config::Logger::QUEUE_SIZE;
    StaticQueue_t            m_queueBuffer;
    uint8_t                  m_queueStorage[QUEUE_LENGTH * sizeof(LogEntry)];

    // Task stack — statically allocated
    static constexpr uint32_t TASK_STACK_WORDS =
        Config::Tasks::STACK_LOGGER;
    StaticTask_t             m_taskTCB;
    StackType_t              m_taskStack[TASK_STACK_WORDS];

    // Backends
    ILogBackend*             m_backends[MAX_BACKENDS];
    uint8_t                  m_backendCount;

    // Statistics
    mutable SemaphoreHandle_t m_statsMutex;
    Stats                     m_stats;
};

// ============================================================
// SerialLogBackend
// Writes formatted entries to UART Serial.
// ============================================================
class SerialLogBackend final : public ILogBackend {
public:
    explicit SerialLogBackend(bool enableColor = true);
    void write(const LogEntry& entry) override;
    void flush()                      override;
    [[nodiscard]] const char* getName() const override { return "Serial"; }

private:
    bool m_enableColor;
    // Scratch buffer for formatting — kept in class to avoid stack pressure
    char m_formatBuffer[220];
};

// ============================================================
// Convenience macros
// These check level at call site to avoid va_list overhead
// when the level is filtered out.
// ============================================================
#define GW_LOG_V(tag, fmt, ...) \
    do { if (static_cast<uint8_t>(Gateway::Services::LogLevel::VERBOSE) >= \
             static_cast<uint8_t>(Gateway::Services::Logger::getInstance().getLevel())) \
        Gateway::Services::Logger::getInstance().verbose(tag, fmt, ##__VA_ARGS__); } while(0)

#define GW_LOG_D(tag, fmt, ...) \
    do { if (static_cast<uint8_t>(Gateway::Services::LogLevel::DEBUG) >= \
             static_cast<uint8_t>(Gateway::Services::Logger::getInstance().getLevel())) \
        Gateway::Services::Logger::getInstance().debug(tag, fmt, ##__VA_ARGS__); } while(0)

#define GW_LOG_I(tag, fmt, ...) \
    do { if (static_cast<uint8_t>(Gateway::Services::LogLevel::INFO) >= \
             static_cast<uint8_t>(Gateway::Services::Logger::getInstance().getLevel())) \
        Gateway::Services::Logger::getInstance().info(tag, fmt, ##__VA_ARGS__); } while(0)

#define GW_LOG_W(tag, fmt, ...) \
    do { if (static_cast<uint8_t>(Gateway::Services::LogLevel::WARNING) >= \
             static_cast<uint8_t>(Gateway::Services::Logger::getInstance().getLevel())) \
        Gateway::Services::Logger::getInstance().warning(tag, fmt, ##__VA_ARGS__); } while(0)

#define GW_LOG_E(tag, fmt, ...) \
    do { if (static_cast<uint8_t>(Gateway::Services::LogLevel::ERROR) >= \
             static_cast<uint8_t>(Gateway::Services::Logger::getInstance().getLevel())) \
        Gateway::Services::Logger::getInstance().error(tag, fmt, ##__VA_ARGS__); } while(0)

#define GW_LOG_C(tag, fmt, ...) \
    Gateway::Services::Logger::getInstance().critical(tag, fmt, ##__VA_ARGS__)

} // namespace Services
} // namespace Gateway

#endif // LOGGER_H