// ============================================================
// SystemConfig.h
// Compile-time constants and hardware configuration
// All tuneable parameters are centralized here.
// ============================================================

#pragma once

#include <arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstddef>

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include <cstdint>

#ifndef FIRMWARE_PROJECT
#define FIRMWARE_PROJECT "ESP-CAM-Server"
#endif
#ifndef FIRMWARE_VENDOR
#define FIRMWARE_VENDOR "Unknown Vendor"
#endif
#ifndef FIRMWARE_BUILD_TYPE
#define FIRMWARE_BUILD_TYPE "Release"
#endif

#ifndef BOOT_MANAGER_STACK_WORDS
#define BOOT_MANAGER_STACK_WORDS 4096U
#endif
#ifndef HEALTH_MONITOR_STACK_WORDS
#define HEALTH_MONITOR_STACK_WORDS 2048U
#endif
#ifndef EVENT_BUS_STACK_WORDS
#define EVENT_BUS_STACK_WORDS 2048U
#endif
#ifndef LOGGER_STACK_WORDS
#define LOGGER_STACK_WORDS 2048U
#endif
#ifndef STATE_MACHINE_STACK_WORDS
#define STATE_MACHINE_STACK_WORDS 4096U
#endif
#ifndef NETWORK_MANAGER_STACK_WORDS
#define NETWORK_MANAGER_STACK_WORDS 4096U
#endif

#ifndef PRIORITY_VALUE_BOOT_MANAGER
#define PRIORITY_VALUE_BOOT_MANAGER 3
#endif
#ifndef PRIORITY_VALUE_HEALTH_MONITOR
#define PRIORITY_VALUE_HEALTH_MONITOR 2
#endif
#ifndef PRIORITY_VALUE_EVENT_BUS
#define PRIORITY_VALUE_EVENT_BUS 2
#endif
#ifndef PRIORITY_VALUE_LOGGER
#define PRIORITY_VALUE_LOGGER 1
#endif
#ifndef PRIORITY_VALUE_STATE_MACHINE
#define PRIORITY_VALUE_STATE_MACHINE 2
#endif
#ifndef PRIORITY_VALUE_NETWORK_MANAGER
#define PRIORITY_VALUE_NETWORK_MANAGER 3
#endif

#ifndef ENABLE_SERIAL_LOGGING
#define ENABLE_SERIAL_LOGGING true
#endif
#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 115200
#endif
#ifndef LOG_LEVEL_DEFAULT
#define LOG_LEVEL_DEFAULT 2
#endif

namespace Gateway {
namespace Config {

// ============================================================
// Firmware Identity
// ============================================================
struct FirmwareInfo {
    static constexpr const char* PROJECT     = FIRMWARE_PROJECT;
    static constexpr const char* VENDOR      = FIRMWARE_VENDOR;
    static constexpr const char* BUILD_TYPE  = FIRMWARE_BUILD_TYPE;

    static constexpr uint8_t VERSION_MAJOR   = 1;
    static constexpr uint8_t VERSION_MINOR   = 0;
    static constexpr uint8_t VERSION_PATCH   = 0;

    static constexpr const char* VERSION_STR = "1.0.0";
};

// ============================================================
// Hardware Configuration
// ============================================================
struct Hardware {
    static constexpr uint32_t CPU_FREQ_MHZ      = 240;
    static constexpr uint32_t FLASH_SIZE_BYTES   = 4 * 1024 * 1024;
    static constexpr uint32_t SRAM_SIZE_BYTES    = 520 * 1024;

    static constexpr uint8_t  LED_STATUS_PIN     = 2;    // built-in LED
    static constexpr uint8_t  INVALID_PIN        = 0xFF;

    static constexpr uint32_t WATCHDOG_TIMEOUT_S = 30;
};

// ============================================================
// FreeRTOS Task Configuration
// ============================================================
struct Tasks {
    // Stack sizes (bytes from build flags; convert to words for FreeRTOS)
    static constexpr uint32_t BOOT_MANAGER_STACK =
        BOOT_MANAGER_STACK_WORDS / sizeof(StackType_t);

    static constexpr uint32_t HEALTH_MONITOR_STACK =
        HEALTH_MONITOR_STACK_WORDS / sizeof(StackType_t);

    static constexpr uint32_t EVENT_BUS_STACK =
        EVENT_BUS_STACK_WORDS / sizeof(StackType_t);

    static constexpr uint32_t LOGGER_STACK =
        LOGGER_STACK_WORDS / sizeof(StackType_t);

    static constexpr uint32_t STATE_MACHINE_STACK =
        STATE_MACHINE_STACK_WORDS / sizeof(StackType_t);

    static constexpr uint32_t NETWORK_MANAGER_STACK =
        NETWORK_MANAGER_STACK_WORDS / sizeof(StackType_t);

    // Exposed task stack sizes used by components.
    static constexpr uint32_t STACK_BOOT_MANAGER = BOOT_MANAGER_STACK_WORDS;
    static constexpr uint32_t STACK_HEALTH_MONITOR = HEALTH_MONITOR_STACK_WORDS;
    static constexpr uint32_t STACK_EVENT_BUS = EVENT_BUS_STACK_WORDS;
    static constexpr uint32_t STACK_LOGGER = LOGGER_STACK_WORDS;
    static constexpr uint32_t STACK_STATE_MACHINE = STATE_MACHINE_STACK_WORDS;
    static constexpr uint32_t STACK_NETWORK_MANAGER = NETWORK_MANAGER_STACK_WORDS;

    // Task priorities
    static constexpr UBaseType_t PRIORITY_BOOT_MANAGER =
        static_cast<UBaseType_t>(PRIORITY_VALUE_BOOT_MANAGER);

    static constexpr UBaseType_t PRIORITY_HEALTH_MONITOR =
        static_cast<UBaseType_t>(PRIORITY_VALUE_HEALTH_MONITOR);

    static constexpr UBaseType_t PRIORITY_EVENT_BUS =
        static_cast<UBaseType_t>(PRIORITY_VALUE_EVENT_BUS);

    static constexpr UBaseType_t PRIORITY_LOGGER =
        static_cast<UBaseType_t>(PRIORITY_VALUE_LOGGER);

    static constexpr UBaseType_t PRIORITY_STATE_MACHINE =
        static_cast<UBaseType_t>(PRIORITY_VALUE_STATE_MACHINE);

    static constexpr UBaseType_t PRIORITY_NETWORK_MANAGER =
        static_cast<UBaseType_t>(PRIORITY_VALUE_NETWORK_MANAGER);

    // Core affinity
    static constexpr BaseType_t CORE_PROTOCOL    = 0;  // WiFi, Async Web
    static constexpr BaseType_t CORE_APPLICATION = 1;  // App logic
    static constexpr BaseType_t CORE_ANY         = tskNO_AFFINITY;
};

// ============================================================
// Network Configuration
// ============================================================
struct Network {
    static constexpr uint16_t HTTP_PORT           = 80;
    static constexpr uint16_t HTTPS_PORT          = 443;   // future
    static constexpr uint16_t WEBSOCKET_PORT      = 80;
    static constexpr uint16_t MDNS_TTL_SECONDS    = 120;

    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS   = 20000;
    static constexpr uint32_t WIFI_RECONNECT_DELAY_MS   = 5000;
    static constexpr uint8_t  WIFI_MAX_RETRY_COUNT       = 10;

    static constexpr const char* MDNS_HOSTNAME    = "gateway";
    static constexpr const char* AP_SSID_PREFIX   = "GW-Setup-";
    static constexpr uint8_t     AP_CHANNEL        = 6;
    static constexpr uint8_t     AP_MAX_CLIENTS    = 4;
};

// ============================================================
// Web Server Configuration
// ============================================================
struct WebServer {
    static constexpr uint16_t MAX_CONCURRENT_CLIENTS  = 8;
    static constexpr uint32_t REQUEST_TIMEOUT_MS       = 10000;
    static constexpr uint32_t WEBSOCKET_TIMEOUT_MS     = 60000;
    static constexpr size_t   MAX_REQUEST_BODY_SIZE    = 4096;    // bytes
    static constexpr size_t   MAX_JSON_RESPONSE_SIZE   = 4096;    // bytes
    static constexpr size_t   STATIC_CACHE_MAX_AGE_S   = 86400;   // 1 day
};

// ============================================================
// Authentication & Session Configuration
// ============================================================
struct Auth {
    static constexpr uint8_t  MAX_SESSIONS             = 8;
    static constexpr uint32_t SESSION_TIMEOUT_S        = 3600;    // 1 hour
    static constexpr uint32_t SESSION_ABSOLUTE_MAX_S   = 86400;   // 24 hours
    static constexpr uint32_t SESSION_ID_BYTES         = 32;      // 256-bit
    static constexpr uint32_t CSRF_TOKEN_BYTES         = 32;      // 256-bit

    static constexpr uint8_t  LOGIN_MAX_ATTEMPTS       = 5;
    static constexpr uint32_t LOGIN_LOCKOUT_S          = 900;     // 15 minutes

    static constexpr uint32_t PBKDF2_ITERATIONS        = 1000;    // ESP32 optimized
    static constexpr uint8_t  PBKDF2_SALT_BYTES        = 16;
    static constexpr uint8_t  PBKDF2_KEY_BYTES         = 32;      // 256-bit output

    static constexpr const char* SESSION_COOKIE_NAME   = "__Host-sid";
    static constexpr const char* CSRF_HEADER_NAME      = "X-CSRF-Token";
    static constexpr const char* CSRF_FORM_FIELD       = "_csrf";

    // Rate limiting
    static constexpr uint8_t  RATE_LIMIT_REQUESTS      = 30;      // per window
    static constexpr uint32_t RATE_LIMIT_WINDOW_S      = 60;
    static constexpr uint8_t  MAX_TRACKED_IPS          = 16;
};

// ============================================================
// Security Configuration
// ============================================================
struct Security {
    static constexpr bool     ENABLE_CSRF_PROTECTION   = true;
    static constexpr bool     ENABLE_XSS_HEADERS       = true;
    static constexpr bool     ENABLE_CLICKJACKING_PROT = true;
    static constexpr bool     ENABLE_CONTENT_SNIFF_PROT= true;
    static constexpr bool     ENABLE_RATE_LIMITING      = true;
    static constexpr bool     SECURE_COOKIE_FLAG        = false;   // true when HTTPS
    static constexpr bool     HTTPONLY_COOKIE_FLAG      = true;
    static constexpr bool     SAMESITE_STRICT           = true;
};

// ============================================================
// Storage Configuration
// ============================================================
struct Storage {
    static constexpr const char* NVS_NAMESPACE_CONFIG  = "gw_config";
    static constexpr const char* NVS_NAMESPACE_AUTH    = "gw_auth";
    static constexpr const char* NVS_NAMESPACE_SYSTEM  = "gw_system";

    static constexpr const char* FS_ROOT               = "/";
    static constexpr const char* FS_WWW_ROOT           = "/www";
    static constexpr const char* FS_CONFIG_DIR         = "/config";
    static constexpr const char* FS_LOGS_DIR           = "/logs";
    static constexpr const char* FS_USERS_FILE         = "/config/users.json";
    static constexpr const char* FS_CONFIG_FILE        = "/config/system.json";

    static constexpr size_t   MAX_FILE_SIZE_BYTES      = 64 * 1024;  // 64KB
    static constexpr uint8_t  MAX_OPEN_FILES           = 4;
};

// ============================================================
// Logger Configuration
// ============================================================
struct Logger {
    static constexpr uint8_t  QUEUE_SIZE               = 16;
    static constexpr size_t   MAX_MESSAGE_LENGTH       = 192;
    static constexpr bool     ENABLE_SERIAL            = ENABLE_SERIAL_LOGGING;
    static constexpr uint32_t SERIAL_BAUD              = SERIAL_BAUD_RATE;
    static constexpr uint8_t  DEFAULT_LEVEL            = LOG_LEVEL_DEFAULT;
};

// ============================================================
// Event Bus Configuration
// ============================================================
struct EventBus {
    static constexpr uint8_t  QUEUE_SIZE               = 24;
    static constexpr uint8_t  MAX_LISTENERS_PER_EVENT  = 8;
    static constexpr uint8_t  MAX_EVENT_TYPES          = 32;
    static constexpr uint32_t DISPATCH_TIMEOUT_MS      = 100;
};

// ============================================================
// Health Monitor Configuration
// ============================================================
struct Health {
    static constexpr uint32_t CHECK_INTERVAL_MS        = 5000;
    static constexpr uint32_t HEAP_WARNING_THRESHOLD   = 32 * 1024; // 32KB
    static constexpr uint32_t HEAP_CRITICAL_THRESHOLD  = 16 * 1024; // 16KB
    static constexpr uint8_t  MAX_CONSECUTIVE_FAILURES = 3;
};

// ============================================================
// User Management
// ============================================================
struct UserConfig {
    static constexpr uint8_t  MAX_USERS               = 10;
    static constexpr uint8_t  USERNAME_MIN_LEN        = 3;
    static constexpr uint8_t  USERNAME_MAX_LEN        = 32;
    static constexpr uint8_t  PASSWORD_MIN_LEN        = 8;
    static constexpr uint8_t  PASSWORD_MAX_LEN        = 64;
    static constexpr const char* DEFAULT_ADMIN_USER   = "admin";
};

// ============================================================
// Future Device Registry
// ============================================================
struct DeviceRegistry {
    static constexpr uint8_t  MAX_CAMERAS             = 4;
    static constexpr uint8_t  MAX_SENSOR_NODES        = 16;
    static constexpr uint8_t  MAX_ACTUATOR_NODES      = 8;
    static constexpr uint32_t DEVICE_HEARTBEAT_S      = 30;
    static constexpr uint32_t DEVICE_TIMEOUT_S        = 90;
};

// ============================================================
// OTA Configuration (Future)
// ============================================================
struct OTA {
    static constexpr uint32_t TIMEOUT_MS              = 120000;  // 2 min
    static constexpr uint16_t PORT                    = 3232;
    static constexpr bool     REQUIRE_AUTH            = true;
};

} // namespace Config
} // namespace Gateway

#endif // SYSTEM_CONFIG_H