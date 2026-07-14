// ============================================================
// AuthManager.h
// Unified authentication and authorization facade.
//
// Design decisions:
//   - Single entry point for all auth operations
//   - Delegates to UserManager + SessionManager
//   - Implements IAuthProvider interface
//   - Integrates RateLimiter for brute-force protection
//   - Provides permission checking for request handlers
//   - Publishes auth events to EventBus
//   - Login audit: log IP, username, result
//   - No direct password handling: delegates to UserManager
// ============================================================

#pragma once

#ifndef AUTH_MANAGER_H
#define AUTH_MANAGER_H

#include "../interfaces/IService.h"
#include "../interfaces/IAuthProvider.h"
#include "../interfaces/IHealthCheck.h"
#include "../models/User.h"
#include "../models/Session.h"
#include "../core/ErrorCodes.h"

namespace Gateway {
namespace Auth {

class AuthManager final
    : public Interfaces::IService
    , public Interfaces::IAuthProvider
    , public Interfaces::IHealthCheck
{
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static AuthManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "AuthManager"; }
    [[nodiscard]] bool         isHealthy() const  override;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "AuthManager"; }

    // --------------------------------------------------------
    // IAuthProvider
    // --------------------------------------------------------
    [[nodiscard]] Result authenticate(
        const char*                       username,
        const char*                       password,
        Interfaces::AuthenticatedUser&    outUser
    ) override;

    [[nodiscard]] Result validateSession(
        const char*                       sessionId,
        Interfaces::AuthenticatedUser&    outUser
    ) override;

    [[nodiscard]] Result createSession(
        const Interfaces::AuthenticatedUser& user,
        char*                                outSessionId,
        size_t                               sessionIdBufferSize
    ) override;

    [[nodiscard]] Result destroySession(
        const char* sessionId
    ) override;

    [[nodiscard]] bool hasPermission(
        const Interfaces::AuthenticatedUser& user,
        const char*                          resource,
        const char*                          action
    ) const override;

    // --------------------------------------------------------
    // Extended auth API (beyond IAuthProvider)
    // --------------------------------------------------------

    // Full login flow: authenticate + rate check + create session
    [[nodiscard]] Result login(
        const char* username,
        const char* password,
        const char* clientIP,
        char*       outSessionId,
        size_t      sessionIdBufferSize
    );

    // Full logout: destroy session + publish event
    [[nodiscard]] Result logout(
        const char* sessionId,
        const char* clientIP
    );

    // Validate session and return full Session object
    [[nodiscard]] Result validateSessionFull(
        const char*      sessionId,
        const char*      clientIP,
        Models::Session& outSession
    );

    // Permission check by session ID (for request handlers)
    [[nodiscard]] bool checkPermission(
        const char*         sessionId,
        const char*         clientIP,
        Models::Permission  required
    );

    // Require minimum role
    [[nodiscard]] bool checkRole(
        const char*         sessionId,
        const char*         clientIP,
        Models::UserRole    minimumRole
    );

private:
    AuthManager();
    ~AuthManager() = default;

    AuthManager(const AuthManager&)            = delete;
    AuthManager& operator=(const AuthManager&) = delete;

    ServiceState m_state;
};

} // namespace Auth
} // namespace Gateway

#endif // AUTH_MANAGER_H