// ============================================================
// mDNSManager.cpp
// ============================================================

#include "mDNSManager.h"
#include "../services/Logger.h"

#include <ESPmDNS.h>
#include <Arduino.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Network {

static constexpr const char* TAG = "mDNSManager";

// ============================================================
// Singleton
// ============================================================
mDNSManager& mDNSManager::getInstance() noexcept {
    static mDNSManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
mDNSManager::mDNSManager()
    : m_initialized(false)
    , m_running(false)
    , m_hostname{}
    , m_services{}
    , m_serviceCount(0)
    , m_mutex(nullptr)
{
}

// ============================================================
// initialize
// ============================================================
Result mDNSManager::initialize(const char* hostname) {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;
    if (!hostname)     return Result::ERR_NULL_POINTER;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    strncpy(m_hostname, hostname, sizeof(m_hostname) - 1);
    m_hostname[sizeof(m_hostname) - 1] = '\0';

    m_initialized = true;
    GW_LOG_I(TAG, "Initialized. Hostname: '%s.local'", m_hostname);
    return Result::OK;
}

// ============================================================
// start
// ============================================================
Result mDNSManager::start() {
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    if (!MDNS.begin(m_hostname)) {
        xSemaphoreGive(m_mutex);
        GW_LOG_E(TAG, "MDNS.begin() failed for hostname '%s'",
                 m_hostname);
        return Result::ERR_MDNS_FAILED;
    }

    m_running = true;

    // Re-register all saved services
    for (uint8_t i = 0; i < m_serviceCount; ++i) {
        if (m_services[i].active) {
            MDNS.addService(m_services[i].serviceType,
                            m_services[i].protocol,
                            m_services[i].port);
        }
    }

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "Started. Reachable at '%s.local'", m_hostname);
    return Result::OK;
}

// ============================================================
// stop
// ============================================================
Result mDNSManager::stop() {
    if (!m_running) return Result::OK;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    MDNS.end();
    m_running = false;

    xSemaphoreGive(m_mutex);
    GW_LOG_I(TAG, "Stopped.");
    return Result::OK;
}

// ============================================================
// restart
// ============================================================
Result mDNSManager::restart() {
    (void)stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    return start();
}

// ============================================================
// isRunning
// ============================================================
bool mDNSManager::isRunning() const noexcept {
    return m_running;
}

// ============================================================
// registerService
// ============================================================
Result mDNSManager::registerService(const char* serviceType,
                                     const char* protocol,
                                     uint16_t    port) {
    if (!serviceType || !protocol) return Result::ERR_NULL_POINTER;
    if (m_serviceCount >= MAX_SERVICES) return Result::ERR_MAX_CAPACITY;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    ServiceRecord& rec = m_services[m_serviceCount];
    strncpy(rec.serviceType, serviceType, sizeof(rec.serviceType) - 1);
    strncpy(rec.protocol,    protocol,    sizeof(rec.protocol) - 1);
    rec.port   = port;
    rec.active = true;
    m_serviceCount++;

    if (m_running) {
        MDNS.addService(serviceType, protocol, port);
    }

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "Service registered: %s.%s port %d",
             serviceType, protocol, static_cast<int>(port));
    return Result::OK;
}

// ============================================================
// addTXTRecord
// ============================================================
Result mDNSManager::addTXTRecord(const char* key,
                                  const char* value) {
    if (!key || !value) return Result::ERR_NULL_POINTER;
    if (!m_running)     return Result::ERR_INVALID_STATE;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    MDNS.addServiceTxt("_http", "_tcp", key, value);

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// queryService
// ============================================================
uint8_t mDNSManager::queryService(const char* serviceType,
                                   const char* protocol) {
    if (!serviceType || !protocol) return 0;
    if (!m_running)                return 0;

    return static_cast<uint8_t>(MDNS.queryService(serviceType,
                                                    protocol));
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport mDNSManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    if (m_running) {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail),
                 "Host:%s.local Services:%d",
                 m_hostname,
                 static_cast<int>(m_serviceCount));
    } else {
        report.status = Interfaces::HealthStatus::DEGRADED;
        snprintf(report.detail, sizeof(report.detail),
                 "mDNS not running");
    }

    return report;
}

} // namespace Network
} // namespace Gateway