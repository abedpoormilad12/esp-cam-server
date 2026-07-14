// ============================================================
// NetworkManager.h
// Unified network orchestrator.
//
// Design decisions:
//   - Owns WiFiManager and mDNSManager instances
//   - Implements IService for lifecycle integration
//   - Listens to EventBus for WiFi state changes
//   - Dedicated FreeRTOS task for reconnect/monitoring loop
//   - Handles mode transitions: STA → AP → STA+AP
//   - Reads WiFi credentials from ConfigManager
//   - After WiFi connects: starts mDNS automatically
//   - Publishes network events to SystemStateMachine
//   - Periodic network health reporting
//
// Startup sequence:
//   1. Load credentials from ConfigManager
//   2. Try STA connection
//   3. If fails → start AP fallback (setup mode)
//   4. If STA succeeds → start mDNS
// ============================================================

#pragma once

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "../interfaces/IService.h"
#include "../interfaces/IEventListener.h"
#include "../interfaces/IHealthCheck.h"
#include "WiFiManager.h"
#include "mDNSManager.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace Gateway {
namespace Network {

class NetworkManager final
    : public Interfaces::IService
    , public Interfaces::IEventListener
    , public Interfaces::IHealthCheck
{
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static NetworkManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] Interfaces::ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "NetworkManager"; }
    [[nodiscard]] bool         isHealthy() const  override;

    // --------------------------------------------------------
    // IEventListener
    // --------------------------------------------------------
    void onEvent(const Interfaces::Event& event) override;

    [[nodiscard]] const Interfaces::EventType*
    getSubscribedEvents(size_t& outCount) const override;

    [[nodiscard]] const char*
    getListenerName() const override { return "NetworkManager"; }

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override {
        return "NetworkManager";
    }

    // --------------------------------------------------------
    // Network access
    // --------------------------------------------------------
    [[nodiscard]] WiFiManager&  getWiFi()  noexcept;
    [[nodiscard]] mDNSManager&  getMDNS()  noexcept;

    [[nodiscard]] bool isNetworkAvailable() const noexcept;
    [[nodiscard]] NetworkInfo getNetworkInfo() const;

    // --------------------------------------------------------
    // Mode switching
    // --------------------------------------------------------
    [[nodiscard]] Result switchToSetupMode();
    [[nodiscard]] Result switchToNormalMode();

private:
    NetworkManager();
    ~NetworkManager() = default;

    NetworkManager(const NetworkManager&)            = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // --------------------------------------------------------
    // Internal connect logic
    // --------------------------------------------------------
    Result connectFromConfig();
    Result startSetupAP();
    Result startMDNS();

    // --------------------------------------------------------
    // FreeRTOS task
    // --------------------------------------------------------
    static void taskEntry(void* param);
    void        task();

    // --------------------------------------------------------
    // Constants
    // --------------------------------------------------------
    static constexpr uint32_t TASK_STACK_WORDS =
        Config::Tasks::STACK_NETWORK_MANAGER;

    static const Interfaces::EventType s_subscribedEvents[];
    static const size_t                s_subscribedEventCount;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState    m_state;
    bool            m_setupMode;

    // Task
    TaskHandle_t    m_taskHandle;
    StaticTask_t    m_taskTCB;
    StackType_t     m_taskStack[TASK_STACK_WORDS];

    SemaphoreHandle_t m_mutex;

    // AP SSID for setup mode
    char            m_setupAPSSID[33];
};

} // namespace Network
} // namespace Gateway

#endif // NETWORK_MANAGER_H