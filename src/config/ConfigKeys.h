// ============================================================
// ConfigKeys.h
// Centralized string constants for all NVS and JSON config keys.
// Using constants prevents typos and enables IDE completion.
// All NVS keys must be ≤ 15 characters.
// ============================================================

#pragma once

#ifndef CONFIG_KEYS_H
#define CONFIG_KEYS_H

namespace Gateway {
namespace Config {

// ============================================================
// NVS Keys — max 15 chars each
// ============================================================
namespace NVSKey {
    // WiFi
    static constexpr const char* WIFI_SSID          = "wifi_ssid";
    static constexpr const char* WIFI_PASS           = "wifi_pass";
    static constexpr const char* WIFI_MODE           = "wifi_mode";

    // System
    static constexpr const char* HOSTNAME            = "hostname";
    static constexpr const char* TIMEZONE            = "timezone";
    static constexpr const char* BOOT_COUNT          = "boot_count";
    static constexpr const char* LAST_BOOT_REASON    = "boot_reason";

    // Security
    static constexpr const char* SEC_SESSION_TMO     = "sec_sess_tmo";
    static constexpr const char* SEC_MAX_SESSIONS    = "sec_max_sess";
    static constexpr const char* SEC_CSRF_ENABLED    = "sec_csrf_en";
    static constexpr const char* SEC_RATE_LIMIT      = "sec_rate_lim";

    // Web
    static constexpr const char* WEB_PORT            = "web_port";
    static constexpr const char* WEB_AUTH_ENABLED    = "web_auth_en";

    // Setup
    static constexpr const char* SETUP_COMPLETE      = "setup_done";
    static constexpr const char* FIRST_BOOT          = "first_boot";
}

// ============================================================
// JSON config file keys (for system.json)
// ============================================================
namespace JsonKey {
    // Top-level sections
    static constexpr const char* SYSTEM              = "system";
    static constexpr const char* NETWORK             = "network";
    static constexpr const char* SECURITY            = "security";
    static constexpr const char* WEB                 = "web";
    static constexpr const char* LOGGING             = "logging";

    // System section
    static constexpr const char* SYS_HOSTNAME        = "hostname";
    static constexpr const char* SYS_TIMEZONE        = "timezone";
    static constexpr const char* SYS_LOG_LEVEL       = "log_level";

    // Network section
    static constexpr const char* NET_SSID            = "ssid";
    static constexpr const char* NET_PASSWORD        = "password";
    static constexpr const char* NET_MODE            = "mode";       // "sta" | "ap" | "both"
    static constexpr const char* NET_AP_SSID         = "ap_ssid";
    static constexpr const char* NET_AP_PASS         = "ap_password";
    static constexpr const char* NET_MDNS            = "mdns";
    static constexpr const char* NET_STATIC_IP       = "static_ip";
    static constexpr const char* NET_GATEWAY         = "gateway";
    static constexpr const char* NET_SUBNET          = "subnet";
    static constexpr const char* NET_DNS             = "dns";

    // Security section
    static constexpr const char* SEC_SESSION_TIMEOUT = "session_timeout";
    static constexpr const char* SEC_CSRF_ENABLED    = "csrf_enabled";
    static constexpr const char* SEC_RATE_LIMIT      = "rate_limit";
    static constexpr const char* SEC_MAX_SESSIONS    = "max_sessions";

    // Web section
    static constexpr const char* WEB_PORT            = "port";
    static constexpr const char* WEB_ENABLE_HTTPS    = "https";

    // Logging section
    static constexpr const char* LOG_LEVEL           = "level";
    static constexpr const char* LOG_SERIAL          = "serial";
}

// ============================================================
// Default values
// ============================================================
namespace Default {
    static constexpr const char* HOSTNAME            = "gateway";
    static constexpr const char* TIMEZONE            = "UTC";
    static constexpr const char* WIFI_MODE           = "sta";
    static constexpr const char* AP_SSID_SUFFIX      = "-setup";
    static constexpr uint16_t    WEB_PORT            = 80;
    static constexpr uint32_t    SESSION_TIMEOUT_S   = 3600;
    static constexpr bool        CSRF_ENABLED        = true;
    static constexpr bool        RATE_LIMIT_ENABLED  = true;
    static constexpr uint8_t     LOG_LEVEL           = 2;   // INFO
}

} // namespace Config
} // namespace Gateway

#endif // CONFIG_KEYS_H