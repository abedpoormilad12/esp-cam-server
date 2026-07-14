// ============================================================
// SessionManager.h
// Server-side session pool management.
//
// Design decisions:
//   - Static pool of MAX_SESSIONS entries (zero heap)
//   - Session ID: 256-bit cryptographically secure random
//   - CSRF token: per-session, generated at creation
//   - IP binding: session tied to client IP
//   - Dual expiry: idle timeout + absolute maximum lifetime
//   - LRU eviction: when pool full, evict oldest session
//   - Thread-safe: single mutex over entire pool
//   - Automatic cleanup task via tick() pattern
//   - No persistent sessions: cleared on restart (by design)
//     Rationale: embedded system restart = security boundary
// ============================================================

#pragma once

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "../interfaces/IService.h"
#include "../interfaces/IHealthCheck.h"
#include "../models/Session.h"
#include "../models/User.h"
#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Auth {

class SessionManager final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    static constexpr uint8_t MAX_SESSIONS = Config::Auth::MAX_SESSIONS;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static SessionManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] Interfaces::ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "SessionManager"; }
    [[nodiscard]] bool         isHealthy() const  override;
    void                       tick()             override;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "SessionManager"; }

    // --------------------------------------------------------
    // Session lifecycle
    // --------------------------------------------------------

    // Create a new session for an authenticated user
    [[nodiscard]] Result createSession(
        const Models::User& user,
        const char*         clientIP,
        char*               outSessionId,
        size_t              sessionIdBufferSize
    );

    // Validate a session by ID
    // On success: touches session (updates lastAccess) and
    // fills outSession with current session data
    [[nodiscard]] Result validateSession(
        const char*     sessionId,
        const char*     clientIP,
        Models::Session& outSession
    );

    // Invalidate (logout) a session
    [[nodiscard]] Result destroySession(const char* sessionId);

    // Invalidate all sessions for a specific user
    [[nodiscard]] Result destroyUserSessions(const char* userId);

    // Invalidate all sessions (system-wide logout)
    [[nodiscard]] Result destroyAllSessions();

    // --------------------------------------------------------
    // Session query
    // --------------------------------------------------------
    [[nodiscard]] uint8_t getActiveSessionCount() const;
    [[nodiscard]] bool    sessionExists(const char* sessionId) const;

    // Get CSRF token for a session
    [[nodiscard]] Result getCSRFToken(
        const char* sessionId,
        char*       outToken,
        size_t      tokenBufferSize
    ) const;

    // --------------------------------------------------------
    // Cleanup: expire stale sessions
    // Called periodically by tick()
    // --------------------------------------------------------
    uint8_t cleanupExpiredSessions();

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------
    struct Stats {
        uint32_t totalCreated;
        uint32_t totalDestroyed;
        uint32_t totalExpired;
        uint32_t totalRejectedIP;
        uint8_t  currentActive;
    };
    [[nodiscard]] Stats getStats() const noexcept;

private:
    SessionManager();
    ~SessionManager() = default;

    SessionManager(const SessionManager&)            = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    Models::Session*       findSession(const char* sessionId);
    const Models::Session* findSession(const char* sessionId) const;
    Models::Session*       findFreeSlot();
    Models::Session*       findLRUSlot();

    static uint32_t nowSeconds() noexcept;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState    m_state;
    Models::Session m_sessions[MAX_SESSIONS];
    SemaphoreHandle_t m_mutex;
    uint32_t        m_lastCleanupMs;
    mutable SemaphoreHandle_t m_statsMutex;
    Stats           m_stats;
};

} // namespace Auth
} // namespace Gateway

#endif // SESSION_MANAGER_H