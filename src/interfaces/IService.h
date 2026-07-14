// ============================================================
// IService.h
// Base interface for all services in the system.
// Every long-running service must implement this interface.
// ============================================================

#pragma once

#ifndef ISERVICE_H
#define ISERVICE_H

#include "../core/ErrorCodes.h"
#include <cstdint>

namespace Gateway {
namespace Interfaces {

// ============================================================
// Service lifecycle states
// ============================================================
enum class ServiceState : uint8_t {
    UNINITIALIZED = 0,
    INITIALIZING,
    RUNNING,
    PAUSED,
    STOPPING,
    STOPPED,
    FAULTED
};

// ============================================================
// IService
// Pure abstract interface for all services.
// Services are long-running components with lifecycle.
// ============================================================
class IService {
public:
    virtual ~IService() = default;

    // Initialize the service and allocate resources
    // Must be called before start()
    [[nodiscard]] virtual Result initialize() = 0;

    // Start the service (may create FreeRTOS tasks)
    [[nodiscard]] virtual Result start() = 0;

    // Gracefully stop the service
    [[nodiscard]] virtual Result stop() = 0;

    // Called periodically for maintenance (optional)
    virtual void tick() {}

    // Query current state
    [[nodiscard]] virtual ServiceState getState() const = 0;

    // Human-readable name for logging
    [[nodiscard]] virtual const char* getName() const = 0;

    // Is the service currently healthy?
    [[nodiscard]] virtual bool isHealthy() const = 0;

protected:
    IService() = default;

    // Prevent copy
    IService(const IService&)            = delete;
    IService& operator=(const IService&) = delete;

    // Allow move
    IService(IService&&)                 = default;
    IService& operator=(IService&&)      = default;
};

} // namespace Interfaces
} // namespace Gateway

#endif // ISERVICE_H