// ============================================================
// ConfigManager.h
// System configuration management with dual-layer storage.
//
// Design decisions:
//   - Two-layer storage: NVS (fast, boot-critical settings) +
//     LittleFS JSON (rich, structured configuration)
//   - In-memory cache: config loaded once at boot, read from
//     RAM thereafter — no repeated flash reads
//   - Write-through: changes written to both cache and storage
//   - Type-safe getters: no string parsing at call site
//   - Change notification via EventBus
//   - Factory reset capability: clears NVS + rewrites defaults
//   - Thread-safe: RW mutex (concurrent reads, exclusive writes)
//   - JSON parsing with ArduinoJson v7
// ============================================================

#pragma once

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "../interfaces/IManager.h"
#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"
#include "ConfigKeys.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Config {

// ============================================================
// WiFi mode enum
// ============================================================
enum class WiFiMode : uint8_t {
    STA  = 0,   // Station only
    AP   = 1,   // Access Point only
    BOTH = 2    // Station + AP simultaneously
};

// ============================================================
// Structured config sections (in-memory representation)
// ============================================================
struct SystemCfg {
    char     hostname[33];
    char     timezone[33];
    uint8_t  logLevel;

    SystemCfg() : hostname{}, timezone{}, logLevel(Default::LOG_LEVEL) {
        strncpy(hostname, Default::HOSTNAME, sizeof(hostname) - 1);
        strncpy(timezone, Default::TIMEZONE, sizeof(timezone) - 1);
    }
};

struct NetworkCfg {
    WiFiMode mode;
    char     ssid[65];
    char     password[65];
    char     apSsid[33];
    char     apPassword[33];
    char     hostname[33];
    bool     mdnsEnabled;
    bool     staticIp;
    char     ipAddress[16];
    char     gateway[16];
    char     subnet[16];
    char     dns[16];

    NetworkCfg()
        : mode(WiFiMode::STA)
        , ssid{}
        , password{}
        , apSsid{}
        , apPassword{}
        , hostname{}
        , mdnsEnabled(true)
        , staticIp(false)
        , ipAddress{}
        , gateway{}
        , subnet{}
        , dns{}
    {}
};

struct SecurityCfg {
    uint32_t sessionTimeoutSeconds;
    uint8_t  maxSessions;
    bool     csrfEnabled;
    bool     rateLimitEnabled;
    uint8_t  rateLimitRequests;
    uint32_t rateLimitWindowSeconds;

    SecurityCfg()
        : sessionTimeoutSeconds(Default::SESSION_TIMEOUT_S)
        , maxSessions(SystemConfig::Auth::MAX_SESSIONS)
        , csrfEnabled(Default::CSRF_ENABLED)
        , rateLimitEnabled(Default::RATE_LIMIT_ENABLED)
        , rateLimitRequests(SystemConfig::Auth::RATE_LIMIT_REQUESTS)
        , rateLimitWindowSeconds(SystemConfig::Auth::RATE_LIMIT_WINDOW_S)
    {}
};

struct WebCfg {
    uint16_t port;
    bool     httpsEnabled;

    WebCfg()
        : port(Default::WEB_PORT)
        , httpsEnabled(false)
    {}
};

// ============================================================
// ConfigManager
// ============================================================
class ConfigManager final : public Interfaces::IManager {
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static ConfigManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IManager
    // --------------------------------------------------------
    [[nodiscard]] Result      initialize()      override;
    [[nodiscard]] bool        isInitialized()   const override;
    [[nodiscard]] const char* getName()         const override { return "ConfigManager"; }

    // --------------------------------------------------------
    // Config section accessors (read — thread-safe)
    // --------------------------------------------------------
    void getSystem  (SystemCfg&  out) const;
    void getNetwork (NetworkCfg& out) const;
    void getSecurity(SecurityCfg& out) const;
    void getWeb     (WebCfg& out)     const;

    // --------------------------------------------------------
    // Individual value getters
    // --------------------------------------------------------
    [[nodiscard]] const char* getHostname()         const noexcept;
    [[nodiscard]] WiFiMode    getWiFiMode()          const noexcept;
    [[nodiscard]] const char* getWiFiSSID()          const noexcept;
    [[nodiscard]] const char* getWiFiPassword()      const noexcept;
    [[nodiscard]] uint16_t    getWebPort()           const noexcept;
    [[nodiscard]] uint32_t    getSessionTimeout()    const noexcept;
    [[nodiscard]] bool        isCSRFEnabled()        const noexcept;
    [[nodiscard]] bool        isRateLimitEnabled()   const noexcept;

    // --------------------------------------------------------
    // Config section setters (write-through to storage)
    // --------------------------------------------------------
    [[nodiscard]] Result setSystem  (const SystemCfg&  cfg);
    [[nodiscard]] Result setNetwork (const NetworkCfg& cfg);
    [[nodiscard]] Result setSecurity(const SecurityCfg& cfg);
    [[nodiscard]] Result setWeb     (const WebCfg& cfg);

    // --------------------------------------------------------
    // Individual critical setters (stored in NVS for fast boot)
    // --------------------------------------------------------
    [[nodiscard]] Result setWiFiCredentials(const char* ssid,
                                             const char* password);

    [[nodiscard]] Result setHostname(const char* hostname);

    // --------------------------------------------------------
    // Persistence
    // --------------------------------------------------------
    [[nodiscard]] Result save();
    [[nodiscard]] Result reload();

    // --------------------------------------------------------
    // Factory reset
    // --------------------------------------------------------
    [[nodiscard]] Result factoryReset();

    // --------------------------------------------------------
    // Setup mode management
    // --------------------------------------------------------
    [[nodiscard]] bool   isSetupComplete() const;
    [[nodiscard]] Result markSetupComplete();
    [[nodiscard]] Result markFirstBoot();

    // --------------------------------------------------------
    // Boot counter
    // --------------------------------------------------------
    [[nodiscard]] uint32_t getBootCount() const;
    [[nodiscard]] Result   incrementBootCount();

private:
    ConfigManager();
    ~ConfigManager() = default;

    ConfigManager(const ConfigManager&)            = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    Result loadFromFile();
    Result saveToFile();
    Result loadFromNVS();
    Result saveToNVS();
    Result applyDefaults();
    Result parseSystemSection(const JsonObject& obj);
    Result parseNetworkSection(const JsonObject& obj);
    Result parseSecuritySection(const JsonObject& obj);
    Result parseWebSection(const JsonObject& obj);
    void   serializeToJson(JsonDocument& doc) const;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool              m_initialized;

    // In-memory config cache
    SystemCfg         m_system;
    NetworkCfg        m_network;
    SecurityCfg       m_security;
    WebCfg            m_web;

    // RW mutex: allows concurrent readers, exclusive writers
    SemaphoreHandle_t m_mutex;

    // JSON document for serialization
    // Static allocation to prevent heap fragmentation
    static constexpr size_t JSON_DOC_SIZE = 2048;
};

// ============================================================
// Convenience alias
// ============================================================
using SystemConfig = ::Gateway::Config::SystemConfig;

} // namespace Config
} // namespace Gateway

#endif // CONFIG_MANAGER_H