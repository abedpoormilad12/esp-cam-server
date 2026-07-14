// ============================================================
// ServiceLocator.cpp
// ============================================================

#include "ServiceLocator.h"
#include "Logger.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Services {

static constexpr const char* TAG = "ServiceLocator";

// ============================================================
// Service name lookup for diagnostics
// ============================================================
static const char* serviceIdToName(ServiceId id) {
    switch (id) {
        case ServiceId::LOGGER:          return "Logger";
        case ServiceId::EVENT_BUS:       return "EventBus";
        case ServiceId::STORAGE_MANAGER: return "StorageManager";
        case ServiceId::CONFIG_MANAGER:  return "ConfigManager";
        case ServiceId::NETWORK_MANAGER: return "NetworkManager";
        case ServiceId::WEB_SERVER:      return "WebServer";
        case ServiceId::AUTH_MANAGER:    return "AuthManager";
        case ServiceId::SESSION_MANAGER: return "SessionManager";
        case ServiceId::USER_MANAGER:    return "UserManager";
        case ServiceId::HEALTH_MONITOR:  return "HealthMonitor";
        case ServiceId::OTA_MANAGER:     return "OTAManager";
        case ServiceId::DEVICE_REGISTRY: return "DeviceRegistry";
        case ServiceId::CAMERA_MANAGER:  return "CameraManager";
        case ServiceId::SENSOR_MANAGER:  return "SensorManager";
        default:                         return "Unknown";
    }
}

static const char* managerIdToName(ManagerId id) {
    switch (id) {
        case ManagerId::CONFIG_MANAGER:  return "ConfigManager";
        case ManagerId::USER_MANAGER:    return "UserManager";
        case ManagerId::DEVICE_REGISTRY: return "DeviceRegistry";
        case ManagerId::CAMERA_MANAGER:  return "CameraManager";
        case ManagerId::SENSOR_MANAGER:  return "SensorManager";
        default:                         return "Unknown";
    }
}

// ============================================================
// Singleton
// ============================================================
ServiceLocator& ServiceLocator::getInstance() noexcept {
    static ServiceLocator instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
ServiceLocator::ServiceLocator()
    : m_services{}
    , m_managers{}
    , m_mutex(nullptr)
    , m_initialized(false)
    , m_registrationLocked(false)
{
    // Zero-initialize all slots
    for (uint8_t i = 0; i < MAX_SERVICES; ++i) {
        m_services[i] = nullptr;
    }
    for (uint8_t i = 0; i < MAX_MANAGERS; ++i) {
        m_managers[i] = nullptr;
    }
}

// ============================================================
// initialize
// ============================================================
Result ServiceLocator::initialize() {
    if (m_initialized) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_mutex = xSemaphoreCreateMutex();
    if (m_mutex == nullptr) {
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_initialized = true;
    return Result::OK;
}

// ============================================================
// registerService
// ============================================================
Result ServiceLocator::registerService(ServiceId id,
                                        Interfaces::IService* service) {
    if (!m_initialized)         return Result::ERR_NOT_INITIALIZED;
    if (service == nullptr)     return Result::ERR_NULL_POINTER;
    if (m_registrationLocked)   return Result::ERR_NOT_SUPPORTED;

    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= MAX_SERVICES)    return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    if (m_services[idx] != nullptr) {
        xSemaphoreGive(m_mutex);
        GW_LOG_W(TAG, "Service '%s' already registered, overwriting",
                 serviceIdToName(id));
    }

    m_services[idx] = service;
    xSemaphoreGive(m_mutex);

    GW_LOG_D(TAG, "Registered service: %s", serviceIdToName(id));
    return Result::OK;
}

// ============================================================
// registerManager
// ============================================================
Result ServiceLocator::registerManager(ManagerId id,
                                        Interfaces::IManager* manager) {
    if (!m_initialized)         return Result::ERR_NOT_INITIALIZED;
    if (manager == nullptr)     return Result::ERR_NULL_POINTER;
    if (m_registrationLocked)   return Result::ERR_NOT_SUPPORTED;

    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= MAX_MANAGERS)    return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    m_managers[idx] = manager;
    xSemaphoreGive(m_mutex);

    GW_LOG_D(TAG, "Registered manager: %s", managerIdToName(id));
    return Result::OK;
}

// ============================================================
// getService
// ============================================================
Interfaces::IService* ServiceLocator::getService(ServiceId id) const noexcept {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= MAX_SERVICES) return nullptr;
    return m_services[idx];
}

// ============================================================
// getManager
// ============================================================
Interfaces::IManager* ServiceLocator::getManager(ManagerId id) const noexcept {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= MAX_MANAGERS) return nullptr;
    return m_managers[idx];
}

// ============================================================
// startAllServices
// Services are started in registration order (by ID value).
// ============================================================
Result ServiceLocator::startAllServices() {
    GW_LOG_I(TAG, "Starting all registered services...");

    for (uint8_t i = 0; i < MAX_SERVICES; ++i) {
        if (m_services[i] == nullptr) continue;

        ServiceId id = static_cast<ServiceId>(i);
        Interfaces::IService* svc = m_services[i];

        GW_LOG_I(TAG, "Starting service: %s", serviceIdToName(id));

        Result r = svc->start();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "Failed to start service '%s': %s",
                     serviceIdToName(id),
                     ResultHelper::toString(r));
            return r;
        }

        GW_LOG_I(TAG, "Service '%s' started successfully",
                 serviceIdToName(id));
    }

    return Result::OK;
}

// ============================================================
// stopAllServices — reverse order
// ============================================================
Result ServiceLocator::stopAllServices() {
    GW_LOG_I(TAG, "Stopping all services...");

    Result lastError = Result::OK;

    // Stop in reverse order
    for (int8_t i = static_cast<int8_t>(MAX_SERVICES) - 1;
         i >= 0; --i) {
        if (m_services[i] == nullptr) continue;

        ServiceId id = static_cast<ServiceId>(i);
        Interfaces::IService* svc = m_services[i];

        Result r = svc->stop();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "Failed to stop service '%s': %s",
                     serviceIdToName(id),
                     ResultHelper::toString(r));
            lastError = r;
            // Continue stopping others despite error
        }
    }

    return lastError;
}

// ============================================================
// lockRegistration
// ============================================================
void ServiceLocator::lockRegistration() noexcept {
    m_registrationLocked = true;
    GW_LOG_I(TAG, "Service registration locked.");
}

bool ServiceLocator::isRegistrationLocked() const noexcept {
    return m_registrationLocked;
}

// ============================================================
// Diagnostics
// ============================================================
uint8_t ServiceLocator::getRegisteredServiceCount() const noexcept {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SERVICES; ++i) {
        if (m_services[i] != nullptr) ++count;
    }
    return count;
}

uint8_t ServiceLocator::getRegisteredManagerCount() const noexcept {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_MANAGERS; ++i) {
        if (m_managers[i] != nullptr) ++count;
    }
    return count;
}

void ServiceLocator::printRegistryStatus() const {
    GW_LOG_I(TAG, "=== Service Registry Status ===");
    GW_LOG_I(TAG, "Services registered: %d / %d",
             static_cast<int>(getRegisteredServiceCount()),
             static_cast<int>(MAX_SERVICES));

    for (uint8_t i = 0; i < MAX_SERVICES; ++i) {
        if (m_services[i] != nullptr) {
            GW_LOG_I(TAG, "  [%02d] %-20s | State: %d | Healthy: %s",
                     static_cast<int>(i),
                     serviceIdToName(static_cast<ServiceId>(i)),
                     static_cast<int>(m_services[i]->getState()),
                     m_services[i]->isHealthy() ? "YES" : "NO");
        }
    }

    GW_LOG_I(TAG, "Managers registered: %d / %d",
             static_cast<int>(getRegisteredManagerCount()),
             static_cast<int>(MAX_MANAGERS));

    for (uint8_t i = 0; i < MAX_MANAGERS; ++i) {
        if (m_managers[i] != nullptr) {
            GW_LOG_I(TAG, "  [%02d] %-20s | Init: %s",
                     static_cast<int>(i),
                     managerIdToName(static_cast<ManagerId>(i)),
                     m_managers[i]->isInitialized() ? "YES" : "NO");
        }
    }

    GW_LOG_I(TAG, "Registration locked: %s",
             m_registrationLocked ? "YES" : "NO");
    GW_LOG_I(TAG, "===============================");
}

} // namespace Services
} // namespace Gateway