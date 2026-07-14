// ============================================================
// RateLimiter.cpp
// ============================================================

#include "RateLimiter.h"

#include <Arduino.h>
#include <cstring>
#include <cmath>

namespace Gateway {
namespace Security {

// ============================================================
// Singleton
// ============================================================
RateLimiter& RateLimiter::getInstance() noexcept {
    static RateLimiter instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
RateLimiter::RateLimiter()
    : m_initialized(false)
    , m_entries{}
    , m_mutex(nullptr)
    , m_stats{}
{
}

// ============================================================
// initialize
// ============================================================
Result RateLimiter::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    for (uint8_t i = 0; i < MAX_IPS; ++i) {
        m_entries[i].reset();
    }

    m_stats      = Stats{};
    m_initialized = true;
    return Result::OK;
}

// ============================================================
// nowSeconds — returns monotonic seconds counter
// ============================================================
uint32_t RateLimiter::nowSeconds() noexcept {
    return static_cast<uint32_t>(millis() / 1000UL);
}

// ============================================================
// findEntry (mutable)
// ============================================================
RateLimiter::IPEntry* RateLimiter::findEntry(const char* ip) {
    for (uint8_t i = 0; i < MAX_IPS; ++i) {
        if (m_entries[i].active &&
            strncmp(m_entries[i].ipAddress, ip, 15) == 0) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

// ============================================================
// findEntry (const)
// ============================================================
const RateLimiter::IPEntry*
RateLimiter::findEntry(const char* ip) const {
    for (uint8_t i = 0; i < MAX_IPS; ++i) {
        if (m_entries[i].active &&
            strncmp(m_entries[i].ipAddress, ip, 15) == 0) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

// ============================================================
// findExpiredSlot — find oldest inactive or expired entry
// ============================================================
RateLimiter::IPEntry* RateLimiter::findExpiredSlot() {
    // First try an inactive slot
    for (uint8_t i = 0; i < MAX_IPS; ++i) {
        if (!m_entries[i].active) return &m_entries[i];
    }

    // Evict oldest entry
    uint8_t  oldest     = 0;
    uint32_t oldestTime = m_entries[0].lastSeen;

    for (uint8_t i = 1; i < MAX_IPS; ++i) {
        if (m_entries[i].lastSeen < oldestTime) {
            oldestTime = m_entries[i].lastSeen;
            oldest     = i;
        }
    }

    return &m_entries[oldest];
}

// ============================================================
// findOrCreateEntry
// ============================================================
RateLimiter::IPEntry*
RateLimiter::findOrCreateEntry(const char* ip) {
    IPEntry* entry = findEntry(ip);
    if (entry) return entry;

    // Create new entry
    entry = findExpiredSlot();
    if (!entry) return nullptr;

    entry->reset();
    strncpy(entry->ipAddress, ip, sizeof(entry->ipAddress) - 1);
    entry->ipAddress[sizeof(entry->ipAddress) - 1] = '\0';
    entry->windowStart  = nowSeconds();
    entry->lastSeen     = nowSeconds();
    entry->active       = true;

    return entry;
}

// ============================================================
// computeEffectiveCount
// Sliding window algorithm:
//   effectiveCount = previousCount * (1 - elapsed/window)
//                  + currentCount
// This smooths out the count over window boundaries.
// ============================================================
float RateLimiter::computeEffectiveCount(
    const IPEntry& entry,
    uint32_t       nowSec,
    uint32_t       windowSeconds) const
{
    uint32_t elapsed = nowSec - entry.windowStart;

    if (elapsed >= windowSeconds * 2) {
        // Entry is stale — more than 2 windows old
        return 0.0f;
    }

    if (elapsed >= windowSeconds) {
        // Current window expired — previous window becomes history
        uint32_t overflowElapsed = elapsed - windowSeconds;
        float    weight = 1.0f - (static_cast<float>(overflowElapsed) /
                                   static_cast<float>(windowSeconds));
        weight = (weight < 0.0f) ? 0.0f : weight;
        return entry.currentWindowCount * weight;
    }

    // Within current window
    float weight = 1.0f - (static_cast<float>(elapsed) /
                            static_cast<float>(windowSeconds));

    return (entry.previousWindowCount * weight) +
            entry.currentWindowCount;
}

// ============================================================
// checkRequest
// ============================================================
Result RateLimiter::checkRequest(
    const char*           ipAddress,
    const RateLimitPolicy& policy)
{
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;
    if (!ipAddress)     return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        // Fail open on mutex timeout to avoid blocking web server
        return Result::OK;
    }

    m_stats.totalRequests++;

    uint32_t  nowSec = nowSeconds();
    IPEntry*  entry  = findOrCreateEntry(ipAddress);

    if (!entry) {
        // Table full — fail open
        xSemaphoreGive(m_mutex);
        return Result::OK;
    }

    entry->lastSeen = nowSec;

    // Check lockout
    if (entry->lockoutUntil > 0 && nowSec < entry->lockoutUntil) {
        m_stats.totalBlocked++;
        xSemaphoreGive(m_mutex);
        return Result::ERR_RATE_LIMITED;
    }

    // Reset lockout if expired
    if (entry->lockoutUntil > 0 && nowSec >= entry->lockoutUntil) {
        entry->lockoutUntil         = 0;
        entry->currentWindowCount   = 0;
        entry->previousWindowCount  = 0;
        entry->windowStart          = nowSec;
    }

    // Advance window if needed
    uint32_t elapsed = nowSec - entry->windowStart;

    if (elapsed >= policy.windowSeconds) {
        if (elapsed >= policy.windowSeconds * 2) {
            // Fully stale
            entry->previousWindowCount = 0;
            entry->currentWindowCount  = 0;
        } else {
            // Roll window
            entry->previousWindowCount = entry->currentWindowCount;
            entry->currentWindowCount  = 0;
        }
        entry->windowStart = nowSec;
    }

    // Compute effective count using sliding window
    float effectiveCount = computeEffectiveCount(
        *entry, nowSec, policy.windowSeconds
    );

    if (effectiveCount >= static_cast<float>(policy.maxRequests)) {
        // Apply lockout
        entry->lockoutUntil = nowSec + policy.lockoutSeconds;
        m_stats.totalBlocked++;
        xSemaphoreGive(m_mutex);
        return Result::ERR_RATE_LIMITED;
    }

    // Consume one credit
    entry->currentWindowCount++;

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// peekRequest (no consumption)
// ============================================================
Result RateLimiter::peekRequest(
    const char*           ipAddress,
    const RateLimitPolicy& policy,
    bool&                  outAllowed) const
{
    outAllowed = true;

    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;
    if (!ipAddress)     return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return Result::OK;  // Fail open
    }

    const IPEntry* entry = findEntry(ipAddress);

    if (!entry) {
        xSemaphoreGive(m_mutex);
        return Result::OK;  // No history = allowed
    }

    uint32_t nowSec = nowSeconds();

    // Check lockout
    if (entry->lockoutUntil > 0 && nowSec < entry->lockoutUntil) {
        outAllowed = false;
        xSemaphoreGive(m_mutex);
        return Result::OK;
    }

    float effectiveCount = computeEffectiveCount(
        *entry, nowSec, policy.windowSeconds
    );

    outAllowed = (effectiveCount < static_cast<float>(policy.maxRequests));

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// resetIP
// ============================================================
Result RateLimiter::resetIP(const char* ipAddress) {
    if (!ipAddress) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    IPEntry* entry = findEntry(ipAddress);
    if (entry) {
        entry->reset();
    }

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// isLockedOut
// ============================================================
bool RateLimiter::isLockedOut(const char* ipAddress) const {
    if (!ipAddress || !m_initialized) return false;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    bool locked = false;
    const IPEntry* entry = findEntry(ipAddress);

    if (entry && entry->lockoutUntil > 0) {
        locked = (nowSeconds() < entry->lockoutUntil);
    }

    xSemaphoreGive(m_mutex);
    return locked;
}

// ============================================================
// getRemainingRequests
// ============================================================
uint8_t RateLimiter::getRemainingRequests(
    const char*           ipAddress,
    const RateLimitPolicy& policy) const
{
    if (!ipAddress || !m_initialized) return policy.maxRequests;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return policy.maxRequests;
    }

    const IPEntry* entry = findEntry(ipAddress);

    if (!entry) {
        xSemaphoreGive(m_mutex);
        return policy.maxRequests;
    }

    float effective = computeEffectiveCount(
        *entry,
        nowSeconds(),
        policy.windowSeconds
    );

    xSemaphoreGive(m_mutex);

    float remaining = static_cast<float>(policy.maxRequests) - effective;
    if (remaining < 0.0f) return 0;

    return static_cast<uint8_t>(remaining);
}

// ============================================================
// cleanup — expire stale entries
// ============================================================
void RateLimiter::cleanup() {
    if (!m_initialized) return;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    uint32_t nowSec          = nowSeconds();
    uint8_t  activeCount     = 0;
    uint32_t staleThreshold  = 3600; // 1 hour

    for (uint8_t i = 0; i < MAX_IPS; ++i) {
        if (m_entries[i].active) {
            uint32_t age = nowSec - m_entries[i].lastSeen;
            if (age > staleThreshold) {
                m_entries[i].reset();
            } else {
                activeCount++;
            }
        }
    }

    m_stats.activeTrackedIPs = activeCount;

    xSemaphoreGive(m_mutex);
}

// ============================================================
// getStats
// ============================================================
RateLimiter::Stats RateLimiter::getStats() const noexcept {
    Stats copy{};
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_mutex);
    }
    return copy;
}

} // namespace Security
} // namespace Gateway