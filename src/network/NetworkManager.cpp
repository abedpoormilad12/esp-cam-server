// ============================================================
// NetworkManager.cpp
// ============================================================

#include "NetworkManager.h"
#include "../config/ConfigManager.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../core/StateMachine.h"

#include <Arduino.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Network {

static constexpr const char* TAG = "NetworkManager";

// ============================================================
// Subscribed events
// ============================================================
const Interfaces::EventType NetworkManager::s_subscribedEvents[] = {
    Interfaces::EventType::NETWORK_WIFI_CONNECTED,
    Interfaces::EventType::NETWORK_WIFI_DISCONNECTED,
    Interfaces::EventType::NETWORK_WIFI_GOT_IP,
    Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED,
};

const size_t NetworkManager::s_subscribedEventCount =
    sizeof(s_subscribedEvents) / sizeof(s_subscribedEvents[0]);

// ============================================================
// Singleton
// ============================================================
NetworkManager& NetworkManager::getInstance() noexcept {
    static NetworkManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
NetworkManager::NetworkManager()
    : m_state(Interfaces::ServiceState::UNINITIALIZED)
    , m_setupMode(false)
    , m_taskHandle(nullptr)
    , m_taskTCB{}
    , m_taskStack{}
    , m_mutex(nullptr)
    , m_setupAPSSID{}
{
}

// ============================================================
// IService::initialize
// ============================================================
Result NetworkManager::initialize() {
    if (m_state != Interfaces::ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = Interfaces::ServiceState::INITIALIZING;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        m_state = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    // Initialize WiFiManager
    Result r = WiFiManager::getInstance().initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "WiFiManager init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    // Initialize mDNSManager
    const char* hostname = Config::ConfigManager::getInstance()
                               .getHostname();
    r = mDNSManager::getInstance().initialize(hostname);
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "mDNS init failed (non-fatal): %s",
                 ResultHelper::toString(r));
        // mDNS failure is non-fatal
    }

    // Subscribe to EventBus
    Services::EventBus::getInstance().subscribe(this);

    m_state = Interfaces::ServiceState::STOPPED;
    GW_LOG_I(TAG, "Initialized.");
    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result NetworkManager::start() {
    if (m_state != Interfaces::ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    // Attempt WiFi connection from saved config
    Result r = connectFromConfig();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Initial WiFi connect failed — starting setup AP");
        startSetupAP();
    }

    // Create monitor task
    m_taskHandle = xTaskCreateStaticPinnedToCore(
        taskEntry,
        "NetworkMgr",
        TASK_STACK_WORDS,
        this,
        Config::Tasks::PRIORITY_NETWORK_MANAGER,
        m_taskStack,
        &m_taskTCB,
        Config::Tasks::CORE_APPLICATION
    );

    if (!m_taskHandle) {
        m_state = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OPERATION_FAILED;
    }

    m_state = Interfaces::ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started.");
    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result NetworkManager::stop() {
    if (m_state != Interfaces::ServiceState::RUNNING &&
        m_state != Interfaces::ServiceState::PAUSED) {
        return Result::ERR_INVALID_STATE;
    }

    m_state = Interfaces::ServiceState::STOPPING;

    Services::EventBus::getInstance().unsubscribe(this);

    mDNSManager::getInstance().stop();
    WiFiManager::getInstance().disconnect();

    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    m_state = Interfaces::ServiceState::STOPPED;
    GW_LOG_I(TAG, "Stopped.");
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool NetworkManager::isHealthy() const {
    if (m_state != Interfaces::ServiceState::RUNNING) return false;

    // Healthy if either: STA connected OR AP active (setup mode)
    return WiFiManager::getInstance().isSTAConnected() ||
           WiFiManager::getInstance().isAPActive();
}

// ============================================================
// IService::getState
// ============================================================
Interfaces::ServiceState NetworkManager::getState() const {
    return m_state;
}

// ============================================================
// isNetworkAvailable
// ============================================================
bool NetworkManager::isNetworkAvailable() const noexcept {
    return WiFiManager::getInstance().isSTAConnected();
}

// ============================================================
// getNetworkInfo
// ============================================================
NetworkInfo NetworkManager::getNetworkInfo() const {
    return WiFiManager::getInstance().getNetworkInfo();
}

// ============================================================
// connectFromConfig
// ============================================================
Result NetworkManager::connectFromConfig() {
    auto& cfg = Config::ConfigManager::getInstance();

    const char* ssid     = cfg.getWiFiSSID();
    const char* password = cfg.getWiFiPassword();

    if (!ssid || strlen(ssid) == 0) {
        GW_LOG_W(TAG, "No WiFi SSID configured.");
        return Result::ERR_CONFIG_KEY_NOT_FOUND;
    }

    GW_LOG_I(TAG, "Connecting to WiFi: '%s'", ssid);

    // Determine network config
    Config::NetworkCfg netCfg;
    cfg.getNetwork(netCfg);

    Result r = WiFiManager::getInstance().connectSTA(
        ssid,
        password,
        nullptr,   // No async callback — we block below
        netCfg.staticIp,
        netCfg.staticIp ? netCfg.ipAddress : nullptr,
        netCfg.staticIp ? netCfg.gateway   : nullptr,
        netCfg.staticIp ? netCfg.subnet    : nullptr
    );

    if (GW_ERR(r)) return r;

    // Wait for connection
    r = WiFiManager::getInstance().waitForConnection(
        Config::Network::WIFI_CONNECT_TIMEOUT_MS
    );

    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "WiFi connection failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    GW_LOG_I(TAG, "WiFi connected. IP: %s",
             WiFiManager::getInstance().getSTAIP());

    // Start mDNS after successful connection
    startMDNS();

    return Result::OK;
}

// ============================================================
// startSetupAP
// ============================================================
Result NetworkManager::startSetupAP() {
    m_setupMode = true;

    // Build AP SSID: "GW-Setup-XXXXXX" using last 3 MAC bytes
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(m_setupAPSSID, sizeof(m_setupAPSSID),
             "%s%02X%02X%02X",
             Config::Network::AP_SSID_PREFIX,
             mac[3], mac[4], mac[5]);

    GW_LOG_I(TAG, "Starting setup AP: '%s'", m_setupAPSSID);

    Result r = WiFiManager::getInstance().startAP(
        m_setupAPSSID,
        nullptr,  // Open network for setup
        Config::Network::AP_CHANNEL
    );

    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Failed to start setup AP: %s",
                 ResultHelper::toString(r));
        return r;
    }

    GW_LOG_I(TAG, "Setup AP active. Connect to '%s' and visit "
             "http://%s to configure.",
             m_setupAPSSID,
             WiFiManager::getInstance().getAPIP());

    return Result::OK;
}

// ============================================================
// startMDNS
// ============================================================
Result NetworkManager::startMDNS() {
    auto& mdns = mDNSManager::getInstance();

    Result r = mdns.start();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "mDNS start failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    // Register HTTP service
    r = mdns.registerService("_http", "_tcp",
                              Config::Network::HTTP_PORT);
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "mDNS HTTP service registration failed");
    }

    // Register gateway discovery service
    r = mdns.registerService("_gateway", "_tcp",
                              Config::Network::HTTP_PORT);

    // Add TXT records for discovery
    mdns.addTXTRecord("version",
                       Config::FirmwareInfo::VERSION_STR);
    mdns.addTXTRecord("vendor",
                       Config::FirmwareInfo::VENDOR);

    GW_LOG_I(TAG, "mDNS started: http://%s.local",
             Config::Network::MDNS_HOSTNAME);

    return Result::OK;
}

// ============================================================
// switchToSetupMode
// ============================================================
Result NetworkManager::switchToSetupMode() {
    GW_LOG_I(TAG, "Switching to setup mode...");

    WiFiManager::getInstance().disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));

    return startSetupAP();
}

// ============================================================
// switchToNormalMode
// ============================================================
Result NetworkManager::switchToNormalMode() {
    GW_LOG_I(TAG, "Switching to normal mode...");

    m_setupMode = false;

    if (WiFiManager::getInstance().isAPActive()) {
        WiFiManager::getInstance().stopAP();
    }

    return connectFromConfig();
}

// ============================================================
// IEventListener::onEvent
// ============================================================
void NetworkManager::onEvent(const Interfaces::Event& event) {
    switch (event.type) {
        case Interfaces::EventType::NETWORK_WIFI_GOT_IP:
            GW_LOG_I(TAG, "Got IP event — ensuring mDNS is running.");
            if (!mDNSManager::getInstance().isRunning()) {
                startMDNS();
            }
            break;

        case Interfaces::EventType::NETWORK_WIFI_DISCONNECTED:
            GW_LOG_W(TAG, "WiFi disconnected event received.");
            mDNSManager::getInstance().stop();
            break;

        case Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED:
            stop();
            break;

        default:
            break;
    }
}

// ============================================================
// getSubscribedEvents
// ============================================================
const Interfaces::EventType*
NetworkManager::getSubscribedEvents(size_t& outCount) const {
    outCount = s_subscribedEventCount;
    return s_subscribedEvents;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport NetworkManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    auto& wifiMgr = WiFiManager::getInstance();
    NetworkInfo info = wifiMgr.getNetworkInfo();

    if (isHealthy()) {
        report.status = Interfaces::HealthStatus::HEALTHY;
        if (info.staConnected) {
            snprintf(report.detail, sizeof(report.detail),
                     "STA:%s RSSI:%d mDNS:%s",
                     info.staIP,
                     static_cast<int>(info.staRSSI),
                     mDNSManager::getInstance().isRunning() ? "OK" : "OFF");
        } else {
            snprintf(report.detail, sizeof(report.detail),
                     "AP-Mode SSID:%s IP:%s",
                     info.apSSID, info.apIP);
        }
    } else {
        report.status = Interfaces::HealthStatus::CRITICAL;
        snprintf(report.detail, sizeof(report.detail),
                 "State:%s",
                 wifiStateToString(info.state));
    }

    return report;
}

// ============================================================
// Accessors
// ============================================================
WiFiManager& NetworkManager::getWiFi() noexcept {
    return WiFiManager::getInstance();
}

mDNSManager& NetworkManager::getMDNS() noexcept {
    return mDNSManager::getInstance();
}

// ============================================================
// FreeRTOS task
// ============================================================
void NetworkManager::taskEntry(void* param) {
    static_cast<NetworkManager*>(param)->task();
}

void NetworkManager::task() {
    GW_LOG_I(TAG, "Monitor task running on core %d",
             static_cast<int>(xPortGetCoreID()));

    static constexpr uint32_t TICK_INTERVAL_MS      = 2000;
    static constexpr uint32_t HEALTH_LOG_INTERVAL_S = 60;

    uint32_t lastHealthLogMs = 0;

    while (true) {
        // Tick WiFiManager (reconnect logic)
        WiFiManager::getInstance().tick();

        // Periodic health logging
        uint32_t now = static_cast<uint32_t>(millis());
        if ((now - lastHealthLogMs) >= HEALTH_LOG_INTERVAL_S * 1000) {
            lastHealthLogMs = now;

            NetworkInfo info = WiFiManager::getInstance().getNetworkInfo();

            if (info.staConnected) {
                GW_LOG_D(TAG,
                         "Network health: IP=%s RSSI=%d Uptime=%lus "
                         "Reconnects=%lu",
                         info.staIP,
                         static_cast<int>(info.staRSSI),
                         static_cast<unsigned long>(info.uptimeSeconds),
                         static_cast<unsigned long>(info.reconnectCount));
            } else if (info.apActive) {
                GW_LOG_D(TAG,
                         "AP mode: SSID=%s Clients=%d",
                         info.apSSID,
                         static_cast<int>(info.apClientCount));
            } else {
                GW_LOG_W(TAG, "Network unavailable. State: %s",
                         wifiStateToString(info.state));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL_MS));
    }
}

} // namespace Network
} // namespace Gateway