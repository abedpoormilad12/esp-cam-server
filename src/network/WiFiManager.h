// ============================================================
// WiFiManager.h
// WiFi connection lifecycle management for ESP32.
//
// Design decisions:
//   - Handles STA (station), AP (access point) and STA+AP modes
//   - Event-driven: uses ESP32 WiFi events via Arduino callbacks
//   - Automatic reconnection with exponential backoff
//   - Static IP support with validation
//   - Thread-safe state access via atomic + mutex
//   - No blocking: all waits via FreeRTOS semaphore signaling
//   - AP mode: activated when STA connect fails (fallback)
//     or explicitly requested (setup mode)
//   - Publishes events to EventBus on state changes
//   - RSSI monitoring for connection quality assessment
//
// Reconnect strategy:
//   Attempt 1-3:   5  second delay
//   Attempt 4-6:   15 second delay
//   Attempt 7+:    60 second delay (max)
//   After MAX_RETRY: transition to FAILED → AP fallback
// ============================================================

#pragma once

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"
#include "../interfaces/IHealthCheck.h"
#include "NetworkState.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include <cstdint>
#include <atomic>
#include <functional>

namespace Gateway {
namespace Network {

// ============================================================
// WiFiManager
// ============================================================
class WiFiManager final : public Interfaces::IHealthCheck {
public:
    // --------------------------------------------------------
    // Connection result callback type
    // --------------------------------------------------------
    using ConnectCallback = std::function<void(bool connected,
                                               const char* ip)>;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static WiFiManager& getInstance() noexcept;

    // --------------------------------------------------------
    // Initialization
    // --------------------------------------------------------
    [[nodiscard]] Result initialize();
    [[nodiscard]] bool   isInitialized() const noexcept;

    // --------------------------------------------------------
    // Connection control
    // --------------------------------------------------------

    // Connect to STA network (non-blocking, result via callback)
    [[nodiscard]] Result connectSTA(const char*     ssid,
                                    const char*     password,
                                    ConnectCallback callback  = nullptr,
                                    bool            staticIp  = false,
                                    const char*     ip        = nullptr,
                                    const char*     gateway   = nullptr,
                                    const char*     subnet    = nullptr);

    // Start Access Point
    [[nodiscard]] Result startAP(const char* ssid,
                                  const char* password = nullptr,
                                  uint8_t     channel  = 6);

    // Stop Access Point
    [[nodiscard]] Result stopAP();

    // Disconnect STA
    [[nodiscard]] Result disconnect();

    // Block until connected or timeout
    [[nodiscard]] Result waitForConnection(uint32_t timeoutMs);

    // Trigger manual reconnect
    [[nodiscard]] Result reconnect();

    // --------------------------------------------------------
    // State query (thread-safe)
    // --------------------------------------------------------
    [[nodiscard]] WiFiState    getState()       const noexcept;
    [[nodiscard]] bool         isSTAConnected() const noexcept;
    [[nodiscard]] bool         isAPActive()     const noexcept;
    [[nodiscard]] bool         hasIPAddress()   const noexcept;
    [[nodiscard]] NetworkInfo  getNetworkInfo() const;

    // --------------------------------------------------------
    // IP address query
    // --------------------------------------------------------
    [[nodiscard]] const char* getSTAIP()      const noexcept;
    [[nodiscard]] const char* getAPIP()       const noexcept;
    [[nodiscard]] const char* getHostname()   const noexcept;
    [[nodiscard]] int8_t      getRSSI()       const noexcept;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "WiFiManager"; }

    // --------------------------------------------------------
    // Reconnect task — call from NetworkManager task
    // --------------------------------------------------------
    void tick();

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------
    struct Stats {
        uint32_t connectAttempts;
        uint32_t connectSuccesses;
        uint32_t disconnectCount;
        uint32_t reconnectCount;
        uint32_t connectedSince;   // millis() when last connected
    };
    [[nodiscard]] Stats getStats() const noexcept;

private:
    WiFiManager();
    ~WiFiManager() = default;

    WiFiManager(const WiFiManager&)            = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;

    // --------------------------------------------------------
    // Static ESP32 WiFi event handlers
    // --------------------------------------------------------
    static void onWiFiEvent(WiFiEvent_t event,
                             WiFiEventInfo_t info);

    // --------------------------------------------------------
    // Internal event handlers
    // --------------------------------------------------------
    void handleSTAConnected(const WiFiEventInfo_t& info);
    void handleSTADisconnected(const WiFiEventInfo_t& info);
    void handleGotIP(const WiFiEventInfo_t& info);
    void handleAPClientConnected(const WiFiEventInfo_t& info);
    void handleAPClientDisconnected(const WiFiEventInfo_t& info);

    // --------------------------------------------------------
    // Reconnect logic
    // --------------------------------------------------------
    void scheduleReconnect();
    void attemptReconnect();
    [[nodiscard]] uint32_t getReconnectDelayMs() const noexcept;

    // --------------------------------------------------------
    // IP string helpers
    // --------------------------------------------------------
    static void ipToString(uint32_t ip, char* out, size_t outSize);

    // --------------------------------------------------------
    // State management
    // --------------------------------------------------------
    void setState(WiFiState newState);

    // --------------------------------------------------------
    // FreeRTOS event bits
    // --------------------------------------------------------
    static constexpr EventBits_t BIT_CONNECTED    = BIT0;
    static constexpr EventBits_t BIT_GOT_IP       = BIT1;
    static constexpr EventBits_t BIT_DISCONNECTED = BIT2;
    static constexpr EventBits_t BIT_FAILED       = BIT3;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool                    m_initialized;
    std::atomic<uint8_t>    m_state;     // WiFiState as uint8_t

    // Credentials storage (for reconnect)
    char                    m_staSSID[65];
    char                    m_staPassword[65];
    bool                    m_staStaticIp;
    char                    m_staIP[16];
    char                    m_staGateway[16];
    char                    m_staSubnet[16];

    // AP settings
    char                    m_apSSID[33];
    char                    m_apPassword[33];
    uint8_t                 m_apChannel;
    bool                    m_apActive;

    // Runtime network info
    char                    m_currentIP[16];
    char                    m_currentAPIP[16];
    char                    m_hostname[33];
    int8_t                  m_rssi;

    // Reconnect management
    uint8_t                 m_reconnectAttempts;
    uint32_t                m_nextReconnectMs;
    bool                    m_reconnectScheduled;

    // FreeRTOS sync
    EventGroupHandle_t      m_eventGroup;
    StaticEventGroup_t      m_eventGroupBuffer;
    SemaphoreHandle_t       m_infoMutex;

    // Callback
    ConnectCallback         m_connectCallback;

    // Statistics
    mutable SemaphoreHandle_t m_statsMutex;
    Stats                     m_stats;
};

} // namespace Network
} // namespace Gateway

#endif // WIFI_MANAGER_H