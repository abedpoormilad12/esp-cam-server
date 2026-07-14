// ============================================================
// mDNSManager.h
// Multicast DNS service advertisement.
//
// Design decisions:
//   - Uses ESP32 Arduino ESPmDNS library
//   - Advertises the gateway as "gateway.local"
//   - Registers HTTP service for browser discovery
//   - Registers custom "_gateway._tcp" service for
//     future device auto-discovery
//   - Restarts mDNS automatically when WiFi reconnects
//   - Thread-safe: operations serialized via mutex
//   - TXT records carry firmware version and capabilities
// ============================================================

#pragma once

#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"
#include "../interfaces/IHealthCheck.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>

namespace Gateway {
namespace Network {

class mDNSManager final : public Interfaces::IHealthCheck {
public:
    // Maximum number of registered services
    static constexpr uint8_t MAX_SERVICES = 4;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static mDNSManager& getInstance() noexcept;

    // --------------------------------------------------------
    // Lifecycle
    // --------------------------------------------------------
    [[nodiscard]] Result initialize(const char* hostname);
    [[nodiscard]] Result start();
    [[nodiscard]] Result stop();
    [[nodiscard]] Result restart();
    [[nodiscard]] bool   isRunning() const noexcept;

    // --------------------------------------------------------
    // Service registration
    // Registers an mDNS service record.
    // serviceType: e.g. "_http"
    // protocol:    e.g. "_tcp"
    // port:        service port number
    // --------------------------------------------------------
    [[nodiscard]] Result registerService(const char* serviceType,
                                          const char* protocol,
                                          uint16_t    port);

    // --------------------------------------------------------
    // TXT record management
    // Add key=value pairs to the last registered service
    // --------------------------------------------------------
    [[nodiscard]] Result addTXTRecord(const char* key,
                                       const char* value);

    // --------------------------------------------------------
    // Query: find other services on the network (future use)
    // --------------------------------------------------------
    [[nodiscard]] uint8_t queryService(const char* serviceType,
                                        const char* protocol);

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override {
        return "mDNSManager";
    }

private:
    mDNSManager();
    ~mDNSManager() = default;

    mDNSManager(const mDNSManager&)            = delete;
    mDNSManager& operator=(const mDNSManager&) = delete;

    // --------------------------------------------------------
    // Service record (for re-registration on reconnect)
    // --------------------------------------------------------
    struct ServiceRecord {
        char     serviceType[24];
        char     protocol[8];
        uint16_t port;
        bool     active;
    };

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool              m_initialized;
    bool              m_running;
    char              m_hostname[33];
    ServiceRecord     m_services[MAX_SERVICES];
    uint8_t           m_serviceCount;
    SemaphoreHandle_t m_mutex;
};

} // namespace Network
} // namespace Gateway

#endif // MDNS_MANAGER_H