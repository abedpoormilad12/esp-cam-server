// ============================================================
// ConfigManager.cpp
// ============================================================

#include "ConfigManager.h"
#include "../storage/StorageManager.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"

#include <Arduino.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Config {

static constexpr const char* TAG = "ConfigManager";

// ============================================================
// Singleton
// ============================================================
ConfigManager& ConfigManager::getInstance() noexcept {
    static ConfigManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
ConfigManager::ConfigManager()
    : m_initialized(false)
    , m_system{}
    , m_network{}
    , m_security{}
    , m_web{}
    , m_mutex(nullptr)
{
}

// ============================================================
// IManager::initialize
// ============================================================
Result ConfigManager::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    // Apply defaults first
    applyDefaults();

    // Load from NVS (fast boot-critical values)
    Result r = loadFromNVS();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "NVS load partial: %s",
                 ResultHelper::toString(r));
    }

    // Load from JSON file (full config)
    bool fileExists = false;
    auto& storageMgr = ::Gateway::Storage::StorageManager::getInstance();
    Result r_exists = storageMgr.fileExists(
        Config::Storage::FS_CONFIG_FILE, fileExists
    );
    if (GW_ERR(r_exists)) return r_exists;

    if (fileExists) {
        r = loadFromFile();
        if (GW_ERR(r)) {
            GW_LOG_W(TAG, "JSON config load failed: %s — using defaults",
                     ResultHelper::toString(r));
        } else {
            GW_LOG_I(TAG, "Config loaded from file.");
        }
    } else {
        GW_LOG_W(TAG, "No config file — using defaults.");
        // Write defaults to file
        saveToFile();
    }

    m_initialized = true;
    GW_LOG_I(TAG, "Initialized. Hostname:'%s' WiFi:'%s'",
             m_system.hostname, m_network.ssid);

    return Result::OK;
}

// ============================================================
// IManager::isInitialized
// ============================================================
bool ConfigManager::isInitialized() const {
    return m_initialized;
}

// ============================================================
// applyDefaults
// ============================================================
Result ConfigManager::applyDefaults() {
    m_system   = SystemCfg{};
    m_network  = NetworkCfg{};
    m_security = SecurityCfg{};
    m_web      = WebCfg{};
    return Result::OK;
}

// ============================================================
// loadFromFile — parse system.json
// ============================================================
Result ConfigManager::loadFromFile() {
    auto& storageMgr = ::Gateway::Storage::StorageManager::getInstance();

    char   jsonBuffer[JSON_DOC_SIZE];
    size_t jsonSize = 0;

    Result r = storageMgr.readJsonFile(
        Config::Storage::FS_CONFIG_FILE,
        jsonBuffer,
        sizeof(jsonBuffer),
        jsonSize
    );

    if (GW_ERR(r)) return r;
    if (jsonSize == 0) return Result::ERR_CONFIG_INVALID;

    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonBuffer, jsonSize);

    if (err) {
        GW_LOG_E(TAG, "JSON parse error: %s", err.c_str());
        return Result::ERR_CONFIG_INVALID;
    }

    // Take write lock
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    if (doc[JsonKey::SYSTEM].is<JsonObject>()) {
        parseSystemSection(doc[JsonKey::SYSTEM].as<JsonObject>());
    }

    if (doc[JsonKey::NETWORK].is<JsonObject>()) {
        parseNetworkSection(doc[JsonKey::NETWORK].as<JsonObject>());
    }

    if (doc[JsonKey::SECURITY].is<JsonObject>()) {
        parseSecuritySection(doc[JsonKey::SECURITY].as<JsonObject>());
    }

    if (doc[JsonKey::WEB].is<JsonObject>()) {
        parseWebSection(doc[JsonKey::WEB].as<JsonObject>());
    }

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// parseSystemSection
// ============================================================
Result ConfigManager::parseSystemSection(const JsonObject& obj) {
    if (obj[JsonKey::SYS_HOSTNAME].is<const char*>()) {
        strncpy(m_system.hostname,
                obj[JsonKey::SYS_HOSTNAME].as<const char*>(),
                sizeof(m_system.hostname) - 1);
    }

    if (obj[JsonKey::SYS_TIMEZONE].is<const char*>()) {
        strncpy(m_system.timezone,
                obj[JsonKey::SYS_TIMEZONE].as<const char*>(),
                sizeof(m_system.timezone) - 1);
    }

    if (obj[JsonKey::SYS_LOG_LEVEL].is<uint8_t>()) {
        m_system.logLevel = obj[JsonKey::SYS_LOG_LEVEL].as<uint8_t>();
    }

    return Result::OK;
}

// ============================================================
// parseNetworkSection
// ============================================================
Result ConfigManager::parseNetworkSection(const JsonObject& obj) {
    if (obj[JsonKey::NET_SSID].is<const char*>()) {
        strncpy(m_network.ssid,
                obj[JsonKey::NET_SSID].as<const char*>(),
                sizeof(m_network.ssid) - 1);
    }

    if (obj[JsonKey::NET_PASSWORD].is<const char*>()) {
        strncpy(m_network.password,
                obj[JsonKey::NET_PASSWORD].as<const char*>(),
                sizeof(m_network.password) - 1);
    }

    if (obj[JsonKey::NET_MODE].is<const char*>()) {
        const char* mode = obj[JsonKey::NET_MODE].as<const char*>();
        if (strcmp(mode, "ap")   == 0) m_network.mode = WiFiMode::AP;
        else if (strcmp(mode, "both") == 0) m_network.mode = WiFiMode::BOTH;
        else                               m_network.mode = WiFiMode::STA;
    }

    if (obj[JsonKey::NET_MDNS].is<bool>()) {
        m_network.mdnsEnabled = obj[JsonKey::NET_MDNS].as<bool>();
    }

    if (obj[JsonKey::NET_STATIC_IP].is<const char*>()) {
        strncpy(m_network.ipAddress,
                obj[JsonKey::NET_STATIC_IP].as<const char*>(),
                sizeof(m_network.ipAddress) - 1);
        m_network.staticIp = (strlen(m_network.ipAddress) > 0);
    }

    if (obj[JsonKey::NET_GATEWAY].is<const char*>()) {
        strncpy(m_network.gateway,
                obj[JsonKey::NET_GATEWAY].as<const char*>(),
                sizeof(m_network.gateway) - 1);
    }

    if (obj[JsonKey::NET_SUBNET].is<const char*>()) {
        strncpy(m_network.subnet,
                obj[JsonKey::NET_SUBNET].as<const char*>(),
                sizeof(m_network.subnet) - 1);
    }

    return Result::OK;
}

// ============================================================
// parseSecuritySection
// ============================================================
Result ConfigManager::parseSecuritySection(const JsonObject& obj) {
    if (obj[JsonKey::SEC_SESSION_TIMEOUT].is<uint32_t>()) {
        m_security.sessionTimeoutSeconds =
            obj[JsonKey::SEC_SESSION_TIMEOUT].as<uint32_t>();
    }

    if (obj[JsonKey::SEC_CSRF_ENABLED].is<bool>()) {
        m_security.csrfEnabled =
            obj[JsonKey::SEC_CSRF_ENABLED].as<bool>();
    }

    if (obj[JsonKey::SEC_RATE_LIMIT].is<bool>()) {
        m_security.rateLimitEnabled =
            obj[JsonKey::SEC_RATE_LIMIT].as<bool>();
    }

    if (obj[JsonKey::SEC_MAX_SESSIONS].is<uint8_t>()) {
        m_security.maxSessions =
            obj[JsonKey::SEC_MAX_SESSIONS].as<uint8_t>();
    }

    return Result::OK;
}

// ============================================================
// parseWebSection
// ============================================================
Result ConfigManager::parseWebSection(const JsonObject& obj) {
    if (obj[JsonKey::WEB_PORT].is<uint16_t>()) {
        m_web.port = obj[JsonKey::WEB_PORT].as<uint16_t>();
    }

    if (obj[JsonKey::WEB_ENABLE_HTTPS].is<bool>()) {
        m_web.httpsEnabled = obj[JsonKey::WEB_ENABLE_HTTPS].as<bool>();
    }

    return Result::OK;
}

// ============================================================
// serializeToJson
// ============================================================
void ConfigManager::serializeToJson(JsonDocument& doc) const {
    // System section
    JsonObject sys = doc[JsonKey::SYSTEM].to<JsonObject>();
    sys[JsonKey::SYS_HOSTNAME]  = m_system.hostname;
    sys[JsonKey::SYS_TIMEZONE]  = m_system.timezone;
    sys[JsonKey::SYS_LOG_LEVEL] = m_system.logLevel;

    // Network section
    JsonObject net = doc[JsonKey::NETWORK].to<JsonObject>();
    net[JsonKey::NET_SSID]      = m_network.ssid;
    net[JsonKey::NET_PASSWORD]  = m_network.password;
    net[JsonKey::NET_MDNS]      = m_network.mdnsEnabled;

    const char* modeStr = "sta";
    if (m_network.mode == WiFiMode::AP)   modeStr = "ap";
    if (m_network.mode == WiFiMode::BOTH) modeStr = "both";
    net[JsonKey::NET_MODE] = modeStr;

    if (m_network.staticIp) {
        net[JsonKey::NET_STATIC_IP] = m_network.ipAddress;
        net[JsonKey::NET_GATEWAY]   = m_network.gateway;
        net[JsonKey::NET_SUBNET]    = m_network.subnet;
    }

    // Security section
    JsonObject sec = doc[JsonKey::SECURITY].to<JsonObject>();
    sec[JsonKey::SEC_SESSION_TIMEOUT] = m_security.sessionTimeoutSeconds;
    sec[JsonKey::SEC_CSRF_ENABLED]    = m_security.csrfEnabled;
    sec[JsonKey::SEC_RATE_LIMIT]      = m_security.rateLimitEnabled;
    sec[JsonKey::SEC_MAX_SESSIONS]    = m_security.maxSessions;

    // Web section
    JsonObject web = doc[JsonKey::WEB].to<JsonObject>();
    web[JsonKey::WEB_PORT]          = m_web.port;
    web[JsonKey::WEB_ENABLE_HTTPS]  = m_web.httpsEnabled;
}

// ============================================================
// saveToFile
// ============================================================
Result ConfigManager::saveToFile() {
    JsonDocument doc;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    serializeToJson(doc);

    xSemaphoreGive(m_mutex);

    char jsonBuffer[JSON_DOC_SIZE];
    size_t written = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

    if (written == 0 || written >= sizeof(jsonBuffer)) {
        return Result::ERR_CONFIG_SAVE_FAILED;
    }

    return ::Gateway::Storage::StorageManager::getInstance().writeJsonFile(
        Config::Storage::FS_CONFIG_FILE, jsonBuffer
    );
}

// ============================================================
// loadFromNVS — load boot-critical values
// ============================================================
Result ConfigManager::loadFromNVS() {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getConfigNVS();

    char  buf[65];
    Result r;

    r = nvs.readString(NVSKey::WIFI_SSID, buf, sizeof(buf));
    if (GW_OK(r) && strlen(buf) > 0) {
        strncpy(m_network.ssid, buf, sizeof(m_network.ssid) - 1);
    }

    r = nvs.readString(NVSKey::WIFI_PASS, buf, sizeof(buf));
    if (GW_OK(r) && strlen(buf) > 0) {
        strncpy(m_network.password, buf, sizeof(m_network.password) - 1);
    }

    r = nvs.readString(NVSKey::HOSTNAME, buf, sizeof(buf));
    if (GW_OK(r) && strlen(buf) > 0) {
        strncpy(m_system.hostname, buf, sizeof(m_system.hostname) - 1);
    }

    return Result::OK;
}

// ============================================================
// saveToNVS — save boot-critical values fast
// ============================================================
Result ConfigManager::saveToNVS() {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getConfigNVS();

    GW_RETURN_IF_ERR(nvs.writeString(NVSKey::WIFI_SSID,
                                      m_network.ssid));
    GW_RETURN_IF_ERR(nvs.writeString(NVSKey::WIFI_PASS,
                                      m_network.password));
    GW_RETURN_IF_ERR(nvs.writeString(NVSKey::HOSTNAME,
                                      m_system.hostname));

    return Result::OK;
}

// ============================================================
// save — persist everything
// ============================================================
Result ConfigManager::save() {
    Result r = saveToFile();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Failed to save config file: %s",
                 ResultHelper::toString(r));
        return r;
    }

    r = saveToNVS();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to save NVS: %s",
                 ResultHelper::toString(r));
    }

    GW_LOG_I(TAG, "Configuration saved.");

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::SYSTEM_STATE_CHANGED
    );

    return Result::OK;
}

// ============================================================
// reload
// ============================================================
Result ConfigManager::reload() {
    applyDefaults();
    loadFromNVS();
    return loadFromFile();
}

// ============================================================
// factoryReset
// ============================================================
Result ConfigManager::factoryReset() {
    GW_LOG_W(TAG, "Factory reset initiated!");

    auto& storageMgr = ::Gateway::Storage::StorageManager::getInstance();

    Result r = storageMgr.getConfigNVS().clear();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to clear config NVS: %s", ResultHelper::toString(r));
    }

    r = storageMgr.getAuthNVS().clear();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to clear auth NVS: %s", ResultHelper::toString(r));
    }

    r = storageMgr.getSystemNVS().clear();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to clear system NVS: %s", ResultHelper::toString(r));
    }

    r = storageMgr.deleteFile(Config::Storage::FS_CONFIG_FILE);
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to delete config file: %s", ResultHelper::toString(r));
    }

    r = storageMgr.deleteFile(Config::Storage::FS_USERS_FILE);
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Failed to delete users file: %s", ResultHelper::toString(r));
    }

    applyDefaults();
    saveToFile();

    GW_LOG_W(TAG, "Factory reset complete. Restart required.");
    return Result::OK;
}

// ============================================================
// Getters
// ============================================================
void ConfigManager::getSystem(SystemCfg& out) const {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        out = m_system;
        xSemaphoreGive(m_mutex);
    }
}

void ConfigManager::getNetwork(NetworkCfg& out) const {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        out = m_network;
        xSemaphoreGive(m_mutex);
    }
}

void ConfigManager::getSecurity(SecurityCfg& out) const {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        out = m_security;
        xSemaphoreGive(m_mutex);
    }
}

void ConfigManager::getWeb(WebCfg& out) const {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        out = m_web;
        xSemaphoreGive(m_mutex);
    }
}

const char* ConfigManager::getHostname() const noexcept {
    return m_system.hostname;
}

WiFiMode ConfigManager::getWiFiMode() const noexcept {
    return m_network.mode;
}

const char* ConfigManager::getWiFiSSID() const noexcept {
    return m_network.ssid;
}

const char* ConfigManager::getWiFiPassword() const noexcept {
    return m_network.password;
}

uint16_t ConfigManager::getWebPort() const noexcept {
    return m_web.port;
}

uint32_t ConfigManager::getSessionTimeout() const noexcept {
    return m_security.sessionTimeoutSeconds;
}

bool ConfigManager::isCSRFEnabled() const noexcept {
    return m_security.csrfEnabled;
}

bool ConfigManager::isRateLimitEnabled() const noexcept {
    return m_security.rateLimitEnabled;
}

// ============================================================
// Setters
// ============================================================
Result ConfigManager::setSystem(const SystemCfg& cfg) {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }
    m_system = cfg;
    xSemaphoreGive(m_mutex);
    return save();
}

Result ConfigManager::setNetwork(const NetworkCfg& cfg) {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }
    m_network = cfg;
    xSemaphoreGive(m_mutex);
    return save();
}

Result ConfigManager::setSecurity(const SecurityCfg& cfg) {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }
    m_security = cfg;
    xSemaphoreGive(m_mutex);
    return save();
}

Result ConfigManager::setWeb(const WebCfg& cfg) {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }
    m_web = cfg;
    xSemaphoreGive(m_mutex);
    return save();
}

Result ConfigManager::setWiFiCredentials(const char* ssid,
                                          const char* password) {
    if (!ssid || !password)    return Result::ERR_NULL_POINTER;
    if (strlen(ssid) == 0)     return Result::ERR_INVALID_ARGUMENT;
    if (strlen(ssid) > 64)     return Result::ERR_INVALID_ARGUMENT;
    if (strlen(password) > 64) return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    strncpy(m_network.ssid,     ssid,     sizeof(m_network.ssid) - 1);
    strncpy(m_network.password, password, sizeof(m_network.password) - 1);

    xSemaphoreGive(m_mutex);
    return save();
}

Result ConfigManager::setHostname(const char* hostname) {
    if (!hostname || strlen(hostname) == 0) return Result::ERR_INVALID_ARGUMENT;
    if (strlen(hostname) > 32)              return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    strncpy(m_system.hostname, hostname, sizeof(m_system.hostname) - 1);

    xSemaphoreGive(m_mutex);
    return save();
}

// ============================================================
// Setup management
// ============================================================
bool ConfigManager::isSetupComplete() const {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getConfigNVS();
    uint32_t val = 0;
    (void)nvs.readUInt32(NVSKey::SETUP_COMPLETE, val);
    return val == 1;
}

Result ConfigManager::markSetupComplete() {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getConfigNVS();
    return nvs.writeUInt32(NVSKey::SETUP_COMPLETE, 1);
}

Result ConfigManager::markFirstBoot() {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getConfigNVS();
    return nvs.writeUInt32(NVSKey::FIRST_BOOT, 0);
}

// ============================================================
// Boot counter
// ============================================================
uint32_t ConfigManager::getBootCount() const {
    auto& nvs = ::Gateway::Storage::StorageManager::getInstance().getSystemNVS();
    uint32_t count = 0;
    (void)nvs.readUInt32(NVSKey::BOOT_COUNT, count);
    return count;
}

Result ConfigManager::incrementBootCount() {
    auto& nvs   = ::Gateway::Storage::StorageManager::getInstance().getSystemNVS();
    uint32_t count = getBootCount();
    return nvs.writeUInt32(NVSKey::BOOT_COUNT, count + 1);
}

} // namespace Config
} // namespace Gateway