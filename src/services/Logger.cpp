// ============================================================
// Logger.cpp
// ============================================================

#include "Logger.h"

#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace Gateway {
namespace Services {

// ============================================================
// Logger — Singleton
// ============================================================
Logger& Logger::getInstance() noexcept {
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_state(ServiceState::UNINITIALIZED)
    , m_level(static_cast<uint8_t>(
          static_cast<LogLevel>(Config::Logger::DEFAULT_LEVEL)))
    , m_queue(nullptr)
    , m_taskHandle(nullptr)
    , m_backendMutex(nullptr)
    , m_queueBuffer{}
    , m_queueStorage{}
    , m_taskTCB{}
    , m_taskStack{}
    , m_backends{}
    , m_backendCount(0)
    , m_statsMutex(nullptr)
    , m_stats{}
{
}

Logger::~Logger() {
    stop();
}

// ============================================================
// IService::initialize
// ============================================================
Result Logger::initialize() {
    if (m_state != ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = ServiceState::INITIALIZING;

    // Create queue with static storage — no heap allocation
    m_queue = xQueueCreateStatic(
        QUEUE_LENGTH,
        sizeof(LogEntry),
        m_queueStorage,
        &m_queueBuffer
    );

    if (m_queue == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    // Create mutexes
    m_backendMutex = xSemaphoreCreateMutex();
    if (m_backendMutex == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_statsMutex = xSemaphoreCreateMutex();
    if (m_statsMutex == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    // Reset statistics
    m_stats = Stats{};

    m_state = ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result Logger::start() {
    if (m_state != ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    // Create logger task with static stack
    m_taskHandle = xTaskCreateStaticPinnedToCore(
        loggerTaskEntry,
        "Logger",
        TASK_STACK_WORDS,
        this,
        Config::Tasks::LOGGER_PRIORITY,
        m_taskStack,
        &m_taskTCB,
        Config::Tasks::CORE_APPLICATION
    );

    if (m_taskHandle == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OPERATION_FAILED;
    }

    m_state = ServiceState::RUNNING;
    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result Logger::stop() {
    if (m_state != ServiceState::RUNNING &&
        m_state != ServiceState::PAUSED) {
        return Result::ERR_INVALID_STATE;
    }

    m_state = ServiceState::STOPPING;

    // Flush remaining entries
    flush(2000);

    // Delete task
    if (m_taskHandle != nullptr) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    m_state = ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool Logger::isHealthy() const {
    if (m_state != ServiceState::RUNNING) return false;
    if (m_queue == nullptr)               return false;

    // Unhealthy if drop rate exceeds 10%
    Logger::Stats s = getStats();
    if (s.totalEnqueued > 100) {
        uint32_t dropPct = (s.totalDropped * 100) / s.totalEnqueued;
        if (dropPct > 10) return false;
    }

    return true;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport Logger::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    if (!isHealthy()) {
        report.status = Interfaces::HealthStatus::DEGRADED;
        Stats s = getStats();
        snprintf(report.detail, sizeof(report.detail),
                 "State:%d Dropped:%lu Queue:%lu",
                 static_cast<int>(m_state),
                 static_cast<unsigned long>(s.totalDropped),
                 static_cast<unsigned long>(s.queueHighWaterMark));
    } else {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail), "Running normally");
    }

    return report;
}

// ============================================================
// addBackend
// ============================================================
Result Logger::addBackend(ILogBackend* backend) {
    if (backend == nullptr) {
        return Result::ERR_NULL_POINTER;
    }

    if (xSemaphoreTake(m_backendMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result result = Result::OK;

    if (m_backendCount >= MAX_BACKENDS) {
        result = Result::ERR_MAX_CAPACITY;
    } else {
        m_backends[m_backendCount++] = backend;
    }

    xSemaphoreGive(m_backendMutex);
    return result;
}

// ============================================================
// Level control
// ============================================================
void Logger::setLevel(LogLevel level) noexcept {
    m_level.store(static_cast<uint8_t>(level),
                  std::memory_order_relaxed);
}

LogLevel Logger::getLevel() const noexcept {
    return static_cast<LogLevel>(
        m_level.load(std::memory_order_relaxed));
}

// ============================================================
// Logging API
// ============================================================
void Logger::log(LogLevel level, const char* tag,
                 const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(level, tag, fmt, args);
    va_end(args);
}

void Logger::verbose(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::VERBOSE, tag, fmt, args);
    va_end(args);
}

void Logger::debug(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::DEBUG, tag, fmt, args);
    va_end(args);
}

void Logger::info(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::INFO, tag, fmt, args);
    va_end(args);
}

void Logger::warning(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::WARNING, tag, fmt, args);
    va_end(args);
}

void Logger::error(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::ERROR, tag, fmt, args);
    va_end(args);
}

void Logger::critical(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    logInternal(LogLevel::CRITICAL, tag, fmt, args);
    va_end(args);
}

// ============================================================
// Internal log dispatch
// ============================================================
void Logger::logInternal(LogLevel level, const char* tag,
                          const char* fmt, va_list args) {
    // Fast compile-time and runtime level check
    if (static_cast<uint8_t>(level) <
        m_level.load(std::memory_order_relaxed)) {
        return;
    }

    // Build entry on stack — no heap involved
    LogEntry entry;
    entry.level     = level;
    entry.timestamp = static_cast<uint32_t>(millis());
    entry.taskId    = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(xTaskGetCurrentTaskHandle()) & 0xFFFFFFFF
    );

    // Copy tag safely
    if (tag != nullptr) {
        strncpy(entry.tag, tag, LogEntry::MAX_TAG_LENGTH - 1);
        entry.tag[LogEntry::MAX_TAG_LENGTH - 1] = '\0';
    }

    // Format message safely
    vsnprintf(entry.message, LogEntry::MAX_MESSAGE_LENGTH, fmt, args);
    entry.message[LogEntry::MAX_MESSAGE_LENGTH - 1] = '\0';

    // Post to queue — non-blocking
    // If in ISR context, use ISR-safe version
    BaseType_t result;
    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        result = xQueueSendToBackFromISR(m_queue, &entry, &woken);
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        result = xQueueSendToBack(m_queue, &entry, 0);
    }

    // Update statistics
    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalEnqueued++;
        if (result != pdTRUE) {
            m_stats.totalDropped++;
        }
        UBaseType_t waterMark = uxQueueMessagesWaiting(m_queue);
        if (waterMark > m_stats.queueHighWaterMark) {
            m_stats.queueHighWaterMark = waterMark;
        }
        xSemaphoreGive(m_statsMutex);
    }
}

// ============================================================
// Dispatch to all registered backends
// ============================================================
void Logger::dispatchToBackends(const LogEntry& entry) {
    if (xSemaphoreTake(m_backendMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (uint8_t i = 0; i < m_backendCount; ++i) {
        if (m_backends[i] != nullptr) {
            m_backends[i]->write(entry);
        }
    }

    xSemaphoreGive(m_backendMutex);

    // Update stats
    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalWritten++;
        xSemaphoreGive(m_statsMutex);
    }
}

// ============================================================
// FreeRTOS task
// ============================================================
void Logger::loggerTaskEntry(void* param) {
    static_cast<Logger*>(param)->loggerTask();
}

void Logger::loggerTask() {
    LogEntry entry;

    while (true) {
        // Block until an entry arrives or timeout
        if (xQueueReceive(m_queue, &entry,
                          pdMS_TO_TICKS(500)) == pdTRUE) {
            dispatchToBackends(entry);
        }

        // Yield if queue is empty
        if (uxQueueMessagesWaiting(m_queue) == 0) {
            taskYIELD();
        }
    }
}

// ============================================================
// Flush
// ============================================================
void Logger::flush(uint32_t timeoutMs) {
    uint32_t deadline = millis() + timeoutMs;

    while (uxQueueMessagesWaiting(m_queue) > 0 &&
           millis() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Flush all backends
    if (xSemaphoreTake(m_backendMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (uint8_t i = 0; i < m_backendCount; ++i) {
            if (m_backends[i] != nullptr) {
                m_backends[i]->flush();
            }
        }
        xSemaphoreGive(m_backendMutex);
    }
}

// ============================================================
// getStats
// ============================================================
Logger::Stats Logger::getStats() const noexcept {
    Stats copy{};
    if (xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_statsMutex);
    }
    return copy;
}

// ============================================================
// ServiceState
// ============================================================
Interfaces::ServiceState Logger::getState() const {
    return m_state;
}

// ============================================================
// SerialLogBackend
// ============================================================
SerialLogBackend::SerialLogBackend(bool enableColor)
    : m_enableColor(enableColor)
    , m_formatBuffer{}
{
}

void SerialLogBackend::write(const LogEntry& entry) {
    if (!Serial) return;

    uint32_t ms   = entry.timestamp;
    uint32_t secs = ms / 1000;
    uint32_t ms_r = ms % 1000;
    uint32_t mins = secs / 60;
    secs          = secs % 60;

    if (m_enableColor) {
        snprintf(m_formatBuffer, sizeof(m_formatBuffer),
                 "%s[%02lu:%02lu.%03lu][%s][%s] %s%s\r\n",
                 logLevelToColor(entry.level),
                 static_cast<unsigned long>(mins),
                 static_cast<unsigned long>(secs),
                 static_cast<unsigned long>(ms_r),
                 logLevelToString(entry.level),
                 entry.tag,
                 entry.message,
                 COLOR_RESET);
    } else {
        snprintf(m_formatBuffer, sizeof(m_formatBuffer),
                 "[%02lu:%02lu.%03lu][%s][%s] %s\r\n",
                 static_cast<unsigned long>(mins),
                 static_cast<unsigned long>(secs),
                 static_cast<unsigned long>(ms_r),
                 logLevelToString(entry.level),
                 entry.tag,
                 entry.message);
    }

    Serial.print(m_formatBuffer);
}

void SerialLogBackend::flush() {
    if (Serial) {
        Serial.flush();
    }
}

} // namespace Services
} // namespace Gateway