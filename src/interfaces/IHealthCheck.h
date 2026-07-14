// ============================================================
// IHealthCheck.h
// Interface for components that report health status.
// Used by HealthMonitor to aggregate system health.
// ============================================================

#pragma once

#ifndef IHEALTH_CHECK_H
#define IHEALTH_CHECK_H

#include <cstdint>

namespace Gateway {
namespace Interfaces {

enum class HealthStatus : uint8_t {
    HEALTHY     = 0,
    DEGRADED    = 1,
    CRITICAL    = 2,
    UNKNOWN     = 3
};

struct HealthReport {
    HealthStatus    status;
    const char*     componentName;
    char            detail[64];
    uint32_t        lastCheckMs;

    HealthReport()
        : status(HealthStatus::UNKNOWN)
        , componentName("unknown")
        , detail{}
        , lastCheckMs(0)
    {}
};

class IHealthCheck {
public:
    virtual ~IHealthCheck() = default;

    [[nodiscard]] virtual HealthReport getHealthReport() const = 0;
    [[nodiscard]] virtual const char*  getComponentName() const = 0;

protected:
    IHealthCheck() = default;

    IHealthCheck(const IHealthCheck&)            = delete;
    IHealthCheck& operator=(const IHealthCheck&) = delete;
};

} // namespace Interfaces
} // namespace Gateway

#endif // IHEALTH_CHECK_H