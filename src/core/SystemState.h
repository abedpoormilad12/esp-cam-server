// ============================================================
// SystemState.h
// System-wide state definitions for the StateMachine.
// Every possible operational state of the gateway is defined
// here. No state logic lives in this file — only data types.
// ============================================================

#pragma once

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <cstdint>

namespace Gateway {
namespace Core {

// ============================================================
// SystemState — all possible gateway states
// ============================================================
enum class SystemState : uint8_t {
    // ---- Startup sequence ----
    POWER_ON            = 0,    // Just powered up, nothing init'd
    HARDWARE_INIT       = 1,    // GPIO, Serial, WDT initializing
    STORAGE_INIT        = 2,    // LittleFS + NVS mounting
    CONFIG_LOAD         = 3,    // Loading configuration
    SECURITY_INIT       = 4,    // Crypto, SecureRandom init
    NETWORK_INIT        = 5,    // WiFi driver init
    SERVICES_INIT       = 6,    // All services initializing
    NETWORK_CONNECT     = 7,    // Connecting to WiFi
    WEBSERVER_INIT      = 8,    // Starting HTTP server

    // ---- Operational states ----
    RUNNING             = 9,    // Fully operational
    DEGRADED            = 10,   // Running with non-critical failures
    MAINTENANCE         = 11,   // OTA or admin maintenance mode

    // ---- Error & recovery ----
    ERROR               = 12,   // Recoverable error
    CRITICAL_ERROR      = 13,   // Unrecoverable, needs restart

    // ---- Shutdown sequence ----
    SHUTTING_DOWN       = 14,   // Graceful shutdown in progress
    RESTARTING          = 15,   // About to call esp_restart()

    // ---- Setup mode (no config found) ----
    SETUP_MODE          = 16,   // AP mode for initial configuration

    _COUNT              = 17,
    INVALID             = 0xFF
};

// ============================================================
// SystemEvent — triggers that drive state transitions
// ============================================================
enum class SystemEvent : uint8_t {
    HARDWARE_INIT_OK        = 0,
    HARDWARE_INIT_FAILED    = 1,
    STORAGE_MOUNTED         = 2,
    STORAGE_FAILED          = 3,
    CONFIG_LOADED           = 4,
    CONFIG_NOT_FOUND        = 5,    // triggers SETUP_MODE
    CONFIG_CORRUPTED        = 6,
    SECURITY_READY          = 7,
    SECURITY_FAILED         = 8,
    NETWORK_DRIVER_READY    = 9,
    NETWORK_DRIVER_FAILED   = 10,
    SERVICES_READY          = 11,
    SERVICES_FAILED         = 12,
    WIFI_CONNECTED          = 13,
    WIFI_FAILED             = 14,
    WEBSERVER_STARTED       = 15,
    WEBSERVER_FAILED        = 16,
    BOOT_COMPLETE           = 17,
    HEALTH_DEGRADED         = 18,
    HEALTH_CRITICAL         = 19,
    HEALTH_RECOVERED        = 20,
    SHUTDOWN_REQUESTED      = 21,
    RESTART_REQUESTED       = 22,
    MAINTENANCE_ENTER       = 23,
    MAINTENANCE_EXIT        = 24,
    ERROR_RECOVERED         = 25,
    SETUP_COMPLETE          = 26,   // exits SETUP_MODE

    _COUNT                  = 27,
    INVALID                 = 0xFF
};

// ============================================================
// Transition descriptor — maps (state, event) → next_state
// ============================================================
struct StateTransition {
    SystemState from;
    SystemEvent event;
    SystemState to;
};

// ============================================================
// Human-readable names (for logging)
// ============================================================
inline constexpr const char* systemStateToString(SystemState s) noexcept {
    switch (s) {
        case SystemState::POWER_ON:         return "POWER_ON";
        case SystemState::HARDWARE_INIT:    return "HARDWARE_INIT";
        case SystemState::STORAGE_INIT:     return "STORAGE_INIT";
        case SystemState::CONFIG_LOAD:      return "CONFIG_LOAD";
        case SystemState::SECURITY_INIT:    return "SECURITY_INIT";
        case SystemState::NETWORK_INIT:     return "NETWORK_INIT";
        case SystemState::SERVICES_INIT:    return "SERVICES_INIT";
        case SystemState::NETWORK_CONNECT:  return "NETWORK_CONNECT";
        case SystemState::WEBSERVER_INIT:   return "WEBSERVER_INIT";
        case SystemState::RUNNING:          return "RUNNING";
        case SystemState::DEGRADED:         return "DEGRADED";
        case SystemState::MAINTENANCE:      return "MAINTENANCE";
        case SystemState::ERROR:            return "ERROR";
        case SystemState::CRITICAL_ERROR:   return "CRITICAL_ERROR";
        case SystemState::SHUTTING_DOWN:    return "SHUTTING_DOWN";
        case SystemState::RESTARTING:       return "RESTARTING";
        case SystemState::SETUP_MODE:       return "SETUP_MODE";
        default:                            return "INVALID";
    }
};

inline constexpr const char* systemEventToString(SystemEvent e) noexcept {
    switch (e) {
        case SystemEvent::HARDWARE_INIT_OK:      return "HARDWARE_INIT_OK";
        case SystemEvent::HARDWARE_INIT_FAILED:  return "HARDWARE_INIT_FAILED";
        case SystemEvent::STORAGE_MOUNTED:       return "STORAGE_MOUNTED";
        case SystemEvent::STORAGE_FAILED:        return "STORAGE_FAILED";
        case SystemEvent::CONFIG_LOADED:         return "CONFIG_LOADED";
        case SystemEvent::CONFIG_NOT_FOUND:      return "CONFIG_NOT_FOUND";
        case SystemEvent::CONFIG_CORRUPTED:      return "CONFIG_CORRUPTED";
        case SystemEvent::SECURITY_READY:        return "SECURITY_READY";
        case SystemEvent::SECURITY_FAILED:       return "SECURITY_FAILED";
        case SystemEvent::NETWORK_DRIVER_READY:  return "NETWORK_DRIVER_READY";
        case SystemEvent::NETWORK_DRIVER_FAILED: return "NETWORK_DRIVER_FAILED";
        case SystemEvent::SERVICES_READY:        return "SERVICES_READY";
        case SystemEvent::SERVICES_FAILED:       return "SERVICES_FAILED";
        case SystemEvent::WIFI_CONNECTED:        return "WIFI_CONNECTED";
        case SystemEvent::WIFI_FAILED:           return "WIFI_FAILED";
        case SystemEvent::WEBSERVER_STARTED:     return "WEBSERVER_STARTED";
        case SystemEvent::WEBSERVER_FAILED:      return "WEBSERVER_FAILED";
        case SystemEvent::BOOT_COMPLETE:         return "BOOT_COMPLETE";
        case SystemEvent::HEALTH_DEGRADED:       return "HEALTH_DEGRADED";
        case SystemEvent::HEALTH_CRITICAL:       return "HEALTH_CRITICAL";
        case SystemEvent::HEALTH_RECOVERED:      return "HEALTH_RECOVERED";
        case SystemEvent::SHUTDOWN_REQUESTED:    return "SHUTDOWN_REQUESTED";
        case SystemEvent::RESTART_REQUESTED:     return "RESTART_REQUESTED";
        case SystemEvent::MAINTENANCE_ENTER:     return "MAINTENANCE_ENTER";
        case SystemEvent::MAINTENANCE_EXIT:      return "MAINTENANCE_EXIT";
        case SystemEvent::ERROR_RECOVERED:       return "ERROR_RECOVERED";
        case SystemEvent::SETUP_COMPLETE:        return "SETUP_COMPLETE";
        default:                                 return "INVALID";
    }
};

}; // namespace Core
}; // namespace Gateway

#endif // SYSTEM_STATE_H