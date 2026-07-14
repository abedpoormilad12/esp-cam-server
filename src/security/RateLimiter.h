// ============================================================
// RateLimiter.h
// Per-IP request rate limiting with sliding window algorithm.
//
// Design decisions:
//   - Fixed-size IP tracking table: no heap allocation
//   - Sliding window counter: more accurate than token bucket
//     for short burst detection
//   - IPv4 only for simplicity (ESP32 WiFi is IPv4)
//   - Separate limits for login endpoint vs general API
//   - Automatic IP entry expiration to prevent table bloat
//   - Thread-safe via FreeRTOS mutex
//   - O(n) lookup where n = MAX_TRACKED_IPS (small, bounded)
//   - Lockout with exponential backoff for repeat offenders
//
// Algorithm: Sliding Window Counter
//   Divides time into windows. Counts requests in current
//   window + weighted count from previous window.
//   This gives smooth rate limiting without bursts at
//   window boundaries.
// ============================================================

#pragma once

#ifndef RATE_LIMITER_H
#define RATE_LIMITER_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Security {

// ============================================================
// Rate limit policy — configurable per endpoint type
// ============================================================
struct RateLimitPolicy {
    uint8_t  maxRequests;       // Max requests per window
    uint32_t windowSeconds;     // Window duration in seconds
    uint32_t lockoutSeconds;    // Lockout duration after limit exceeded

    static constexpr RateLimitPolicy general() {
        return {
            Config::Auth::RATE_LIMIT_REQUESTS,
            Config::Auth::RATE_LIMIT_WINDOW_S,
            60
        };
    }

    static constexpr RateLimitPolicy login() {
        return {
            Config::Auth::LOGIN_MAX_ATTEMPTS,
            300,    // 5-minute window for login
            Config::Auth::LOGIN_LOCKOUT_S
        };
    }

    static constexpr RateLimitPolicy strict() {
        return { 10, 60, 300 };
    }
};

// ============================================================
// RateLimiter
// ============================================================
class RateLimiter final {
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static RateLimiter& getInstance() noexcept;

    // --------------------------------------------------------
    // Initialize
    // --------------------------------------------------------
    [[nodiscard]] Result initialize();

    // --------------------------------------------------------
    // Check and consume one request credit for an IP.
    // Returns OK if allowed, ERR_RATE_LIMITED if blocked.
    // --------------------------------------------------------
    [[nodiscard]] Result checkRequest(
        const char*           ipAddress,
        const RateLimitPolicy& policy
    );

    // --------------------------------------------------------
    // Check without consuming (for pre-validation)
    // --------------------------------------------------------
    [[nodiscard]] Result peekRequest(
        const char*           ipAddress,
        const RateLimitPolicy& policy,
        bool&                  outAllowed
    ) const;

    // --------------------------------------------------------
    // Manual reset for an IP (admin use)
    // --------------------------------------------------------
    Result resetIP(const char* ipAddress);

    // --------------------------------------------------------
    // Check if IP is currently locked out
    // --------------------------------------------------------
    [[nodiscard]] bool isLockedOut(const char* ipAddress) const;

    // --------------------------------------------------------
    // Get remaining requests in current window for IP
    // --------------------------------------------------------
    [[nodiscard]] uint8_t getRemainingRequests(
        const char*           ipAddress,
        const RateLimitPolicy& policy
    ) const;

    // --------------------------------------------------------
    // Housekeeping: expire old entries
    // Call periodically (e.g., every 60 seconds)
    // --------------------------------------------------------
    void cleanup();

    // --------------------------------------------------------
    // Diagnostics
    // --------------------------------------------------------
    struct Stats {
        uint32_t totalRequests;
        uint32_t totalBlocked;
        uint8_t  activeTrackedIPs;
    };

    [[nodiscard]] Stats getStats() const noexcept;

private:
    RateLimiter();
    ~RateLimiter() = default;

    RateLimiter(const RateLimiter&)            = delete;
    RateLimiter& operator=(const RateLimiter&) = delete;

    // --------------------------------------------------------
    // Per-IP tracking entry
    // Uses sliding window with two counters
    // --------------------------------------------------------
    struct IPEntry {
        char     ipAddress[16];         // "255.255.255.255\0"
        uint32_t windowStart;           // Timestamp of current window start (seconds)
        uint16_t currentWindowCount;    // Requests in current window
        uint16_t previousWindowCount;   // Requests in previous window
        uint32_t lockoutUntil;          // Unix timestamp of lockout end (0 = not locked)
        uint32_t lastSeen;              // For expiration
        bool     active;

        IPEntry()
            : ipAddress{}
            , windowStart(0)
            , currentWindowCount(0)
            , previousWindowCount(0)
            , lockoutUntil(0)
            , lastSeen(0)
            , active(false)
        {}

        void reset() {
            memset(ipAddress, 0, sizeof(ipAddress));
            windowStart         = 0;
            currentWindowCount  = 0;
            previousWindowCount = 0;
            lockoutUntil        = 0;
            lastSeen            = 0;
            active              = false;
        }
    };

    static constexpr uint8_t MAX_IPS = Config::Auth::MAX_TRACKED_IPS;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    IPEntry*       findEntry(const char* ip);
    const IPEntry* findEntry(const char* ip) const;
    IPEntry*       findOrCreateEntry(const char* ip);
    IPEntry*       findExpiredSlot();

    float computeEffectiveCount(const IPEntry& entry,
                                uint32_t       nowSeconds,
                                uint32_t       windowSeconds) const;

    static uint32_t nowSeconds() noexcept;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool              m_initialized;
    IPEntry           m_entries[MAX_IPS];
    SemaphoreHandle_t m_mutex;
    Stats             m_stats;
};

} // namespace Security
} // namespace Gateway

#endif // RATE_LIMITER_H