// ============================================================
// StorageManager.cpp
// ============================================================

#include "StorageManager.h"
#include "../services/Logger.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Storage {

static constexpr const char* TAG = "StorageManager";

// ============================================================
// Singleton
// ============================================================
StorageManager& StorageManager::getInstance() noexcept {
    static StorageManager instance;
    return instance;
}

// ============================================================
// Constructor — initialize NVS instances per namespace
// ============================================================
StorageManager::StorageManager()
    : m_nvsConfig(Config::Storage::NVS_NAMESPACE_CONFIG, false)
    , m_nvsAuth  (Config::Storage::NVS_NAMESPACE_AUTH,   false)
    , m_nvsSystem(Config::Storage::NVS_NAMESPACE_SYSTEM,  false)
    , m_littleFS (Config::Storage::FS_ROOT)
    , m_state(Interfaces::ServiceState::UNINITIALIZED)
{
}

// ============================================================
// IService::initialize
// ============================================================
Result StorageManager::initialize() {
    if (m_state != Interfaces::ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = Interfaces::ServiceState::INITIALIZING;

    // Initialize NVS backends
    Result r = m_nvsConfig.initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "NVS config init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    r = m_nvsAuth.initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "NVS auth init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    r = m_nvsSystem.initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "NVS system init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    // Initialize LittleFS backend
    r = m_littleFS.initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "LittleFS init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    m_state = Interfaces::ServiceState::STOPPED;
    GW_LOG_I(TAG, "All storage backends initialized.");
    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result StorageManager::start() {
    if (m_state != Interfaces::ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    m_state = Interfaces::ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started.");
    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result StorageManager::stop() {
    m_state = Interfaces::ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::getState
// ============================================================
Interfaces::ServiceState StorageManager::getState() const {
    return m_state;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool StorageManager::isHealthy() const {
    if (m_state != Interfaces::ServiceState::RUNNING) return false;
    if (!m_nvsConfig.isReady())           return false;
    if (!m_littleFS.isReady())            return false;
    if (isStorageLow())                   return false;
    return true;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport StorageManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    if (!isHealthy()) {
        report.status = Interfaces::HealthStatus::DEGRADED;

        size_t free = getFreeSpaceBytes();
        snprintf(report.detail, sizeof(report.detail),
                 "State:%d NVS:%s FS:%s Free:%zuKB",
                 static_cast<int>(m_state),
                 m_nvsConfig.isReady() ? "OK" : "ERR",
                 m_littleFS.isReady()  ? "OK" : "ERR",
                 free / 1024);
    } else {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail),
                 "Free:%zuKB",
                 getFreeSpaceBytes() / 1024);
    }

    return report;
}

// ============================================================
// getNVS — returns a named NVS instance
// For now returns config NVS; in future can create on-demand
// ============================================================
NVSStorage& StorageManager::getNVS(const char* namespaceName) {
    // Route to the appropriate pre-created namespace
    if (namespaceName) {
        if (strcmp(namespaceName, Config::Storage::NVS_NAMESPACE_AUTH) == 0) {
            return m_nvsAuth;
        }
        if (strcmp(namespaceName, Config::Storage::NVS_NAMESPACE_SYSTEM) == 0) {
            return m_nvsSystem;
        }
    }
    return m_nvsConfig;
}

// ============================================================
// getFS
// ============================================================
LittleFSStorage& StorageManager::getFS() {
    return m_littleFS;
}

// ============================================================
// readJsonFile
// ============================================================
Result StorageManager::readJsonFile(const char* path,
                                     char*       outBuffer,
                                     size_t      bufferSize,
                                     size_t&     outSize) {
    if (!outBuffer) return Result::ERR_NULL_POINTER;

    Result r = m_littleFS.readString(path, outBuffer, bufferSize);
    if (GW_OK(r)) {
        outSize = strlen(outBuffer);
    } else {
        outSize = 0;
    }

    return r;
}

// ============================================================
// writeJsonFile
// ============================================================
Result StorageManager::writeJsonFile(const char* path,
                                      const char* jsonContent) {
    if (!jsonContent) return Result::ERR_NULL_POINTER;
    return m_littleFS.writeString(path, jsonContent);
}

// ============================================================
// fileExists
// ============================================================
Result StorageManager::fileExists(const char* path,
                                   bool&       outExists) {
    return m_littleFS.exists(path, outExists);
}

// ============================================================
// deleteFile
// ============================================================
Result StorageManager::deleteFile(const char* path) {
    return m_littleFS.remove(path);
}

// ============================================================
// isStorageLow
// ============================================================
bool StorageManager::isStorageLow() const {
    size_t total = 0, used = 0, free = 0;
    Result r = const_cast<LittleFSStorage&>(m_littleFS).getFSStats(
        total, used, free
    );
    if (GW_ERR(r)) return true; // Consider low if we can't get stats
    // Warn if less than 10% free
    return (total > 0) && (free < total / 10);
}

// ============================================================
// getFreeSpaceBytes
// ============================================================
size_t StorageManager::getFreeSpaceBytes() const {
    size_t total = 0, used = 0, free = 0;
    Result r = const_cast<LittleFSStorage&>(m_littleFS).getFSStats(
        total, used, free
    );
    return GW_OK(r) ? free : 0;
}

} // namespace Storage
} // namespace Gateway