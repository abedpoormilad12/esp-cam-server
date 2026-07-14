// ============================================================
// NetworkState.h
// Network-specific state and event definitions.
// Separate from SystemState to keep concerns isolated.
// ============================================================

#pragma once

#ifndef NETWORK_STATE_H
#define NETWORK_STATE_H

#include <cstdint>

namespace Gateway {
namespace Network {

// ============================================================
// WiFi operational state
// ============================================================
enum class WiFiState : uint8_t {
    IDLE            = 0,
    SCANNING        = 1,
    CONNECTING      = 2,
    CONNECTED       = 3,
    GETTING_IP      = 4,
    GOT_IP          = 5,
    DISCONNECTED    = 6,
    RECONNECTING    = 7,
    AP_ACTIVE       = 8,
    AP_STA_ACTIVE   = 9,
    FAILED          = 10
};

// ============================================================
// Network interface info snapshot
// ============================================================
struct NetworkInfo {
    // STA interface
    char     staSSID[33];
    char     staIP[16];
    char     staGateway[16];
    char     staSubnet[16];
    char     staMac[18];
    int8_t   staRSSI;
    bool     staConnected;

    // AP interface
    char     apSSID[33];
    char     apIP[16];
    char     apMac[18];
    uint8_t  apClientCount;
    bool     apActive;

    // General
    char     hostname[33];
    WiFiState state;
    uint32_t  uptimeSeconds;
    uint32_t  reconnectCount;

    NetworkInfo()
        : staSSID{}
        , staIP{}
        , staGateway{}
        , staSubnet{}
        , staMac{}
        , staRSSI(0)
        , staConnected(false)
        , apSSID{}
        , apIP{}
        , apMac{}
        , apClientCount(0)
        , apActive(false)
        , hostname{}
        , state(WiFiState::IDLE)
        , uptimeSeconds(0)
        , reconnectCount(0)
    {}
};

// ============================================================
// String helpers
// ============================================================
inline constexpr const char* wifiStateToString(WiFiState s) noexcept {
    switch (s) {
        case WiFiState::IDLE:           return "IDLE";
        case WiFiState::SCANNING:       return "SCANNING";
        case WiFiState::CONNECTING:     return "CONNECTING";
        case WiFiState::CONNECTED:      return "CONNECTED";
        case WiFiState::GETTING_IP:     return "GETTING_IP";
        case WiFiState::GOT_IP:         return "GOT_IP";
        case WiFiState::DISCONNECTED:   return "DISCONNECTED";
        case WiFiState::RECONNECTING:   return "RECONNECTING";
        case WiFiState::AP_ACTIVE:      return "AP_ACTIVE";
        case WiFiState::AP_STA_ACTIVE:  return "AP_STA_ACTIVE";
        case WiFiState::FAILED:         return "FAILED";
        default:                        return "UNKNOWN";
    }
}

} // namespace Network
} // namespace Gateway

#endif // NETWORK_STATE_H