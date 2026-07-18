// ============================================================
// WiFiManager.cpp
// ============================================================

#include "WiFiManager.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Network {

static constexpr const char* TAG = "WiFiManager";

// ============================================================
// Singleton
// ============================================================
WiFiManager& WiFiManager::getInstance() noexcept {
    static WiFiManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
WiFiManager::WiFiManager()
    : m_initialized(false)
    , m_state(static_cast<uint8_t>(WiFiState::IDLE))
    , m_staSSID{}
    , m_staPassword{}
    , m_staStaticIp(false)
    , m_staIP{}
    , m_staGateway{}
    , m_staSubnet{}
    , m_apSSID{}
    , m_apPassword{}
    , m_apChannel(6)
    , m_apActive(false)
    , m_currentIP{}
    , m_currentAPIP{}
    , m_hostname{}
    , m_rssi(0)
    , m_reconnectAttempts(0)
    , m_nextReconnectMs(0)
    , m_reconnectScheduled(false)
    , m_eventGroup(nullptr)
    , m_eventGroupBuffer{}
    , m_infoMutex(nullptr)
    , m_connectCallback(nullptr)
    , m_statsMutex(nullptr)
    , m_stats{}
{
}

// ============================================================
// initialize
// ============================================================
Result WiFiManager::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_eventGroup = xEventGroupCreateStatic(&m_eventGroupBuffer);
    if (!m_eventGroup) return Result::ERR_OUT_OF_MEMORY;

    m_infoMutex = xSemaphoreCreateMutex();
    if (!m_infoMutex) return Result::ERR_OUT_OF_MEMORY;

    m_statsMutex = xSemaphoreCreateMutex();
    if (!m_statsMutex) return Result::ERR_OUT_OF_MEMORY;

    // Register event handler
    WiFi.onEvent(onWiFiEvent);

    // Set WiFi mode to NONE initially
    WiFi.mode(WIFI_OFF);

    // Configure power save off for reliability
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Set hostname
    strncpy(m_hostname,
            Config::Network::MDNS_HOSTNAME,
            sizeof(m_hostname) - 1);

    m_initialized = true;
    GW_LOG_I(TAG, "Initialized.");
    return Result::OK;
}

// ============================================================
// isInitialized
// ============================================================
bool WiFiManager::isInitialized() const noexcept {
    return m_initialized;
}

// ============================================================
// setState — atomic update + logging
// ============================================================
void WiFiManager::setState(WiFiState newState) {
    WiFiState old = static_cast<WiFiState>(
        m_state.exchange(static_cast<uint8_t>(newState),
                          std::memory_order_release)
    );

    if (old != newState) {
        GW_LOG_I(TAG, "State: %s → %s",
                 wifiStateToString(old),
                 wifiStateToString(newState));
    }
}

// ============================================================
// connectSTA
// ============================================================
Result WiFiManager::connectSTA(const char*     ssid,
                                const char*     password,
                                ConnectCallback callback,
                                bool            staticIp,
                                const char*     ip,
                                const char*     gateway,
                                const char*     subnet) {
    if (!m_initialized)        return Result::ERR_NOT_INITIALIZED;
    if (!ssid || !ssid[0])     return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_infoMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    // Store credentials for reconnect
    strncpy(m_staSSID, ssid, sizeof(m_staSSID) - 1);
    m_staSSID[sizeof(m_staSSID) - 1] = '\0';

    if (password) {
        strncpy(m_staPassword, password, sizeof(m_staPassword) - 1);
        m_staPassword[sizeof(m_staPassword) - 1] = '\0';
    } else {
        m_staPassword[0] = '\0';
    }

    m_staStaticIp = staticIp;

    if (staticIp && ip && gateway && subnet) {
        strncpy(m_staIP,      ip,      sizeof(m_staIP) - 1);
        strncpy(m_staGateway, gateway, sizeof(m_staGateway) - 1);
        strncpy(m_staSubnet,  subnet,  sizeof(m_staSubnet) - 1);
    }

    m_connectCallback = callback;

    xSemaphoreGive(m_infoMutex);

    // Clear event bits
    xEventGroupClearBits(m_eventGroup,
                          BIT_CONNECTED | BIT_GOT_IP |
                          BIT_DISCONNECTED | BIT_FAILED);

    setState(WiFiState::CONNECTING);

    // Configure WiFi mode
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(m_hostname);

    // Configure static IP if requested
    if (staticIp && ip && gateway && subnet) {
        IPAddress ipAddr, gwAddr, snAddr;
        if (ipAddr.fromString(ip) &&
            gwAddr.fromString(gateway) &&
            snAddr.fromString(subnet)) {
            WiFi.config(ipAddr, gwAddr, snAddr);
        }
    }

    // Begin connection (non-blocking)
    WiFi.begin(m_staSSID, m_staPassword[0] ? m_staPassword : nullptr);

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.connectAttempts++;
        xSemaphoreGive(m_statsMutex);
    }

    GW_LOG_I(TAG, "Connecting to SSID: '%s'", m_staSSID);
    return Result::OK;
}

// ============================================================
// waitForConnection — blocks until connected or timeout
// ============================================================
Result WiFiManager::waitForConnection(uint32_t timeoutMs) {
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;

    EventBits_t bits = xEventGroupWaitBits(
        m_eventGroup,
        BIT_GOT_IP | BIT_FAILED | BIT_DISCONNECTED,
        pdFALSE,    // Don't clear on exit
        pdFALSE,    // Wait for ANY bit
        pdMS_TO_TICKS(timeoutMs)
    );

    if (bits & BIT_GOT_IP) {
        return Result::OK;
    }

    if (bits & BIT_FAILED) {
        return Result::ERR_WIFI_CONNECT_FAILED;
    }

    // Timeout
    GW_LOG_W(TAG, "Connection timeout after %lu ms",
             static_cast<unsigned long>(timeoutMs));
    return Result::ERR_WIFI_TIMEOUT;
}

// ============================================================
// startAP
// ============================================================
Result WiFiManager::startAP(const char* ssid,
                              const char* password,
                              uint8_t     channel) {
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;
    if (!ssid || !ssid[0]) return Result::ERR_INVALID_ARGUMENT;

    strncpy(m_apSSID, ssid, sizeof(m_apSSID) - 1);
    m_apSSID[sizeof(m_apSSID) - 1] = '\0';

    if (password) {
        strncpy(m_apPassword, password, sizeof(m_apPassword) - 1);
        m_apPassword[sizeof(m_apPassword) - 1] = '\0';
    } else {
        m_apPassword[0] = '\0';
    }

    m_apChannel = channel;

    // Determine if STA is also active
    bool staActive = isSTAConnected();
    wifi_mode_t mode = staActive ? WIFI_AP_STA : WIFI_AP;

    WiFi.mode(mode);

    bool ok = WiFi.softAP(
        m_apSSID,
        m_apPassword[0] ? m_apPassword : nullptr,
        channel,
        0,   // hidden = false
        Config::Network::AP_MAX_CLIENTS
    );

    if (!ok) {
        GW_LOG_E(TAG, "AP start failed");
        return Result::ERR_WIFI_CONNECT_FAILED;
    }

    m_apActive = true;

    if (xSemaphoreTake(m_infoMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        IPAddress apIp = WiFi.softAPIP();
        snprintf(m_currentAPIP, sizeof(m_currentAPIP),
                 "%d.%d.%d.%d",
                 apIp[0], apIp[1], apIp[2], apIp[3]);
        xSemaphoreGive(m_infoMutex);
    }

    setState(staActive ? WiFiState::AP_STA_ACTIVE : WiFiState::AP_ACTIVE);

    GW_LOG_I(TAG, "AP started. SSID:'%s' IP:%s",
             m_apSSID, m_currentAPIP);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::NETWORK_AP_CLIENT_CONNECTED
    );

    return Result::OK;
}

// ============================================================
// stopAP
// ============================================================
Result WiFiManager::stopAP() {
    if (!m_apActive) return Result::OK;

    WiFi.softAPdisconnect(true);
    m_apActive = false;

    if (isSTAConnected()) {
        WiFi.mode(WIFI_STA);
        setState(WiFiState::GOT_IP);
    } else {
        setState(WiFiState::IDLE);
    }

    GW_LOG_I(TAG, "AP stopped.");
    return Result::OK;
}

// ============================================================
// disconnect
// ============================================================
Result WiFiManager::disconnect() {
    WiFi.disconnect(false);
    setState(WiFiState::DISCONNECTED);
    m_reconnectScheduled  = false;
    m_reconnectAttempts   = 0;
    return Result::OK;
}

// ============================================================
// reconnect
// ============================================================
Result WiFiManager::reconnect() {
    if (!m_staSSID[0]) return Result::ERR_NOT_INITIALIZED;

    GW_LOG_I(TAG, "Reconnecting to '%s' (attempt %d)...",
             m_staSSID,
             static_cast<int>(m_reconnectAttempts + 1));

    setState(WiFiState::RECONNECTING);
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFi.begin(m_staSSID, m_staPassword[0] ? m_staPassword : nullptr);

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.reconnectCount++;
        xSemaphoreGive(m_statsMutex);
    }

    return Result::OK;
}

// ============================================================
// getReconnectDelayMs — exponential backoff
// ============================================================
uint32_t WiFiManager::getReconnectDelayMs() const noexcept {
    if (m_reconnectAttempts < 3)  return 5000;
    if (m_reconnectAttempts < 6)  return 15000;
    return 60000;
}

// ============================================================
// scheduleReconnect
// ============================================================
void WiFiManager::scheduleReconnect() {
    if (m_reconnectAttempts >= Config::Network::WIFI_MAX_RETRY_COUNT) {
        GW_LOG_W(TAG, "Max reconnect attempts reached (%d). Giving up.",
                 static_cast<int>(Config::Network::WIFI_MAX_RETRY_COUNT));
        setState(WiFiState::FAILED);

        Services::EventBus::getInstance().publish(
            Interfaces::EventType::NETWORK_WIFI_DISCONNECTED,
            "max_retries"
        );
        return;
    }

    uint32_t delay        = getReconnectDelayMs();
    m_nextReconnectMs     = static_cast<uint32_t>(millis()) + delay;
    m_reconnectScheduled  = true;
    m_reconnectAttempts++;

    GW_LOG_I(TAG, "Reconnect scheduled in %lu ms (attempt %d/%d)",
             static_cast<unsigned long>(delay),
             static_cast<int>(m_reconnectAttempts),
             static_cast<int>(Config::Network::WIFI_MAX_RETRY_COUNT));
}

// ============================================================
// tick — called from NetworkManager task
// ============================================================
void WiFiManager::tick() {
    if (!m_initialized) return;

    // Periodic RSSI update
    if (isSTAConnected()) {
        m_rssi = WiFi.RSSI();
    }

    // Process scheduled reconnect
    if (m_reconnectScheduled) {
        uint32_t now = static_cast<uint32_t>(millis());
        if (now >= m_nextReconnectMs) {
            m_reconnectScheduled = false;
            attemptReconnect();
        }
    }
}

// ============================================================
// attemptReconnect
// ============================================================
void WiFiManager::attemptReconnect() {
(void)reconnect();
}

// ============================================================
// Static WiFi event handler
// ============================================================
void WiFiManager::onWiFiEvent(WiFiEvent_t event,
                               WiFiEventInfo_t info) {
    WiFiManager& mgr = getInstance();

    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            mgr.handleSTAConnected(info);
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            mgr.handleSTADisconnected(info);
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            mgr.handleGotIP(info);
            break;

        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            mgr.handleAPClientConnected(info);
            break;

        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            mgr.handleAPClientDisconnected(info);
            break;

        default:
            break;
    }
}

// ============================================================
// handleSTAConnected
// ============================================================
void WiFiManager::handleSTAConnected(const WiFiEventInfo_t& info) {
    setState(WiFiState::GETTING_IP);
    GW_LOG_I(TAG, "STA connected — waiting for IP...");
}

// ============================================================
// handleSTADisconnected
// ============================================================
void WiFiManager::handleSTADisconnected(const WiFiEventInfo_t& info) {
    uint8_t reason = info.wifi_sta_disconnected.reason;

    if (xSemaphoreTake(m_infoMutex, 0) == pdTRUE) {
        memset(m_currentIP, 0, sizeof(m_currentIP));
        xSemaphoreGive(m_infoMutex);
    }

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.disconnectCount++;
        xSemaphoreGive(m_statsMutex);
    }

    xEventGroupSetBits(m_eventGroup, BIT_DISCONNECTED);

    GW_LOG_W(TAG, "STA disconnected. Reason: %d", static_cast<int>(reason));

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::NETWORK_WIFI_DISCONNECTED,
        static_cast<uint32_t>(reason)
    );

    // Auth failed — don't retry automatically
    if (reason == WIFI_REASON_AUTH_FAIL ||
        reason == WIFI_REASON_NO_AP_FOUND) {
        setState(WiFiState::FAILED);
        xEventGroupSetBits(m_eventGroup, BIT_FAILED);

        if (m_connectCallback) {
            m_connectCallback(false, nullptr);
            m_connectCallback = nullptr;
        }
        return;
    }

    setState(WiFiState::DISCONNECTED);
    scheduleReconnect();
}

// ============================================================
// handleGotIP
// ============================================================
void WiFiManager::handleGotIP(const WiFiEventInfo_t& info) {
    if (xSemaphoreTake(m_infoMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t ip = info.got_ip.ip_info.ip.addr;
        ipToString(ip, m_currentIP, sizeof(m_currentIP));
        xSemaphoreGive(m_infoMutex);
    }

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.connectSuccesses++;
        m_stats.connectedSince = static_cast<uint32_t>(millis());
        xSemaphoreGive(m_statsMutex);
    }

    m_reconnectAttempts  = 0;
    m_reconnectScheduled = false;

    setState(WiFiState::GOT_IP);
    xEventGroupSetBits(m_eventGroup, BIT_GOT_IP);

    GW_LOG_I(TAG, "IP acquired: %s", m_currentIP);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::NETWORK_WIFI_GOT_IP,
        info.got_ip.ip_info.ip.addr
    );

    if (m_connectCallback) {
        m_connectCallback(true, m_currentIP);
        m_connectCallback = nullptr;
    }
}

// ============================================================
// handleAPClientConnected
// ============================================================
void WiFiManager::handleAPClientConnected(
    const WiFiEventInfo_t& info)
{
    GW_LOG_I(TAG, "AP client connected. Clients: %d",
             static_cast<int>(WiFi.softAPgetStationNum()));

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::NETWORK_AP_CLIENT_CONNECTED
    );
}

// ============================================================
// handleAPClientDisconnected
// ============================================================
void WiFiManager::handleAPClientDisconnected(
    const WiFiEventInfo_t& info)
{
    GW_LOG_I(TAG, "AP client disconnected. Clients: %d",
             static_cast<int>(WiFi.softAPgetStationNum()));

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::NETWORK_AP_CLIENT_DISCONNECTED
    );
}

// ============================================================
// ipToString — converts uint32_t IP to dotted-decimal
// ============================================================
void WiFiManager::ipToString(uint32_t  ip,
                               char*     out,
                               size_t    outSize) {
    snprintf(out, outSize, "%lu.%lu.%lu.%lu",
             static_cast<unsigned long>((ip)       & 0xFF),
             static_cast<unsigned long>((ip >>  8) & 0xFF),
             static_cast<unsigned long>((ip >> 16) & 0xFF),
             static_cast<unsigned long>((ip >> 24) & 0xFF));
}

// ============================================================
// State/info accessors
// ============================================================
WiFiState WiFiManager::getState() const noexcept {
    return static_cast<WiFiState>(
        m_state.load(std::memory_order_acquire)
    );
}

bool WiFiManager::isSTAConnected() const noexcept {
    WiFiState s = getState();
    return s == WiFiState::GOT_IP || s == WiFiState::AP_STA_ACTIVE;
}

bool WiFiManager::isAPActive() const noexcept {
    return m_apActive;
}

bool WiFiManager::hasIPAddress() const noexcept {
    return m_currentIP[0] != '\0';
}

const char* WiFiManager::getSTAIP() const noexcept {
    return m_currentIP;
}

const char* WiFiManager::getAPIP() const noexcept {
    return m_currentAPIP;
}

const char* WiFiManager::getHostname() const noexcept {
    return m_hostname;
}

int8_t WiFiManager::getRSSI() const noexcept {
    return m_rssi;
}

// ============================================================
// getNetworkInfo — full snapshot
// ============================================================
NetworkInfo WiFiManager::getNetworkInfo() const {
    NetworkInfo info;

    if (xSemaphoreTake(m_infoMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // STA info
        strncpy(info.staSSID, m_staSSID, sizeof(info.staSSID) - 1);
        strncpy(info.staIP,   m_currentIP, sizeof(info.staIP) - 1);
        info.staConnected = isSTAConnected();
        info.staRSSI      = m_rssi;

        if (info.staConnected) {
            IPAddress gw = WiFi.gatewayIP();
            IPAddress sn = WiFi.subnetMask();
            snprintf(info.staGateway, sizeof(info.staGateway),
                     "%d.%d.%d.%d", gw[0], gw[1], gw[2], gw[3]);
            snprintf(info.staSubnet, sizeof(info.staSubnet),
                     "%d.%d.%d.%d", sn[0], sn[1], sn[2], sn[3]);

            // MAC address
            uint8_t mac[6];
            WiFi.macAddress(mac);
            snprintf(info.staMac, sizeof(info.staMac),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }

        // AP info
        strncpy(info.apSSID, m_apSSID, sizeof(info.apSSID) - 1);
        strncpy(info.apIP,   m_currentAPIP, sizeof(info.apIP) - 1);
        info.apActive      = m_apActive;
        info.apClientCount = static_cast<uint8_t>(
            WiFi.softAPgetStationNum()
        );

        // General
        strncpy(info.hostname, m_hostname, sizeof(info.hostname) - 1);
        info.state = getState();

        xSemaphoreGive(m_infoMutex);
    }

    if (xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        info.reconnectCount = m_stats.reconnectCount;
        if (m_stats.connectedSince > 0) {
            info.uptimeSeconds = (millis() - m_stats.connectedSince) / 1000;
        }
        xSemaphoreGive(m_statsMutex);
    }

    return info;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport WiFiManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    WiFiState state = getState();

    if (state == WiFiState::GOT_IP ||
        state == WiFiState::AP_ACTIVE ||
        state == WiFiState::AP_STA_ACTIVE) {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail),
                 "State:%s IP:%s RSSI:%d",
                 wifiStateToString(state),
                 m_currentIP,
                 static_cast<int>(m_rssi));
    } else if (state == WiFiState::RECONNECTING ||
               state == WiFiState::CONNECTING) {
        report.status = Interfaces::HealthStatus::DEGRADED;
        snprintf(report.detail, sizeof(report.detail),
                 "State:%s Attempt:%d",
                 wifiStateToString(state),
                 static_cast<int>(m_reconnectAttempts));
    } else {
        report.status = Interfaces::HealthStatus::CRITICAL;
        snprintf(report.detail, sizeof(report.detail),
                 "State:%s", wifiStateToString(state));
    }

    return report;
}

// ============================================================
// getStats
// ============================================================
WiFiManager::Stats WiFiManager::getStats() const noexcept {
    Stats copy{};
    if (xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_statsMutex);
    }
    return copy;
}

} // namespace Network
} // namespace Gateway