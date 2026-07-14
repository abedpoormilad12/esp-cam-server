// ============================================================
// ServiceLocator.h
// Central registry for all services and managers.
//
// Design decisions:
//   - Provides controlled global access to services
//   - Replaces scattered global variables with typed access
//   - NOT a replacement for constructor injection
//     (use DI where possible; use ServiceLocator only where
//      DI is impractical in embedded context)
//   - Registration happens once during boot
//   - Lookup is O(n) over a small fixed array — acceptable
//   - Thread-safe reads after initialization
//   - No heap allocation: fixed-size slot table
// ============================================================

#pragma once

#ifndef SERVICE_LOCATOR_H
#define SERVICE_LOCATOR_H

#include "../interfaces/IService.h"
#include "../interfaces/IManager.h"
#include "../core/ErrorCodes.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Services {

// ============================================================
// Service slot identifiers — typed enum prevents string lookup
// ============================================================
enum class ServiceId : uint8_t {
    LOGGER              = 0,
    EVENT_BUS           = 1,
    STORAGE_MANAGER     = 2,
    CONFIG_MANAGER      = 3,
    NETWORK_MANAGER     = 4,
    WEB_SERVER          = 5,
    AUTH_MANAGER        = 6,
    SESSION_MANAGER     = 7,
    USER_MANAGER        = 8,
    HEALTH_MONITOR      = 9,
    OTA_MANAGER         = 10,   // future
    DEVICE_REGISTRY     = 11,   // future
    CAMERA_MANAGER      = 12,   // future
    SENSOR_MANAGER      = 13,   // future

    _COUNT              = 14    // always last
};

enum class ManagerId : uint8_t {
    CONFIG_MANAGER      = 0,
    USER_MANAGER        = 1,
    DEVICE_REGISTRY     = 2,   // future
    CAMERA_MANAGER      = 3,   // future
    SENSOR_MANAGER      = 4,   // future

    _COUNT              = 5
};

// ============================================================
// ServiceLocator
// Singleton registry that holds pointers to all services.
// ============================================================
class ServiceLocator final {
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static ServiceLocator& getInstance() noexcept;

    // --------------------------------------------------------
    // Initialization — call once before any lookups
    // --------------------------------------------------------
    [[nodiscard]] Result initialize();

    // --------------------------------------------------------
    // Service registration
    // Only allowed during boot phase.
    // --------------------------------------------------------
    [[nodiscard]] Result registerService(ServiceId id,
                                         Interfaces::IService* service);

    [[nodiscard]] Result registerManager(ManagerId id,
                                          Interfaces::IManager* manager);

    // --------------------------------------------------------
    // Service lookup
    // Returns nullptr if not registered.
    // Thread-safe after initialization completes.
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::IService* getService(ServiceId id) const noexcept;
    [[nodiscard]] Interfaces::IManager* getManager(ManagerId id) const noexcept;

    // --------------------------------------------------------
    // Typed convenience accessors
    // Avoids casting at call sites.
    // --------------------------------------------------------
    template<typename T>
    [[nodiscard]] T* getServiceAs(ServiceId id) const noexcept {
        return static_cast<T*>(getService(id));
    }

    template<typename T>
    [[nodiscard]] T* getManagerAs(ManagerId id) const noexcept {
        return static_cast<T*>(getManager(id));
    }

    // --------------------------------------------------------
    // Lifecycle: start all registered services in order
    // --------------------------------------------------------
    [[nodiscard]] Result startAllServices();
    [[nodiscard]] Result stopAllServices();

    // --------------------------------------------------------
    // Diagnostics
    // --------------------------------------------------------
    uint8_t getRegisteredServiceCount() const noexcept;
    uint8_t getRegisteredManagerCount() const noexcept;
    void    printRegistryStatus() const;

    // --------------------------------------------------------
    // Lock/unlock registration (after boot, prevent late reg)
    // --------------------------------------------------------
    void lockRegistration() noexcept;
    [[nodiscard]] bool isRegistrationLocked() const noexcept;

private:
    ServiceLocator();
    ~ServiceLocator() = default;

    ServiceLocator(const ServiceLocator&)            = delete;
    ServiceLocator& operator=(const ServiceLocator&) = delete;
    ServiceLocator(ServiceLocator&&)                 = delete;
    ServiceLocator& operator=(ServiceLocator&&)      = delete;

    // --------------------------------------------------------
    // Storage — fixed-size arrays, no heap
    // --------------------------------------------------------
    static constexpr uint8_t MAX_SERVICES =
        static_cast<uint8_t>(ServiceId::_COUNT);
    static constexpr uint8_t MAX_MANAGERS =
        static_cast<uint8_t>(ManagerId::_COUNT);

    Interfaces::IService* m_services[MAX_SERVICES];
    Interfaces::IManager* m_managers[MAX_MANAGERS];

    SemaphoreHandle_t     m_mutex;
    bool                  m_initialized;
    bool                  m_registrationLocked;
};

// ============================================================
// Global convenience accessor
// Reduces verbosity at call sites while keeping
// the singleton pattern explicit.
// ============================================================
[[nodiscard]] inline ServiceLocator& SL() noexcept {
    return ServiceLocator::getInstance();
}

} // namespace Services
} // namespace Gateway

#endif // SERVICE_LOCATOR_H