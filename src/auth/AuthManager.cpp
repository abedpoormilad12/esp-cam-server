// ============================================================
// AuthManager.cpp
// ============================================================

#include "AuthManager.h"
#include "SessionManager.h"
#include "../managers/UserManager.h"
#include "../security/RateLimiter.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../interfaces/IService.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Auth {

static constexpr const char* TAG = "AuthManager";

// ============================================================
// Singleton
// ============================================================
AuthManager& AuthManager::getInstance() noexcept {
    static AuthManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
AuthManager::AuthManager()
    : m_state(Interfaces::ServiceState::UNINITIALIZED)
{
}

// ============================================================
// IService::initialize
// ============================================================
Result AuthManager::initialize() {
    if (m_state != Interfaces::ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = Interfaces::ServiceState::INITIALIZING;

    // Initialize RateLimiter
    Result r = Security::RateLimiter::getInstance().initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "RateLimiter init failed: %s",
                 ResultHelper::toString(r));
        m_state = Interfaces::ServiceState::FAULTED;
        return r;
    }

    m_state = Interfaces::ServiceState::STOPPED;
    GW_LOG_I(TAG, "Initialized.");
    return Result::OK;
}

// ============================================================
// IService::start / stop
// ============================================================
Result AuthManager::start() {
    if (m_state != Interfaces::ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }
    m_state = Interfaces::ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started.");
    return Result::OK;
}

Result AuthManager::stop() {
    m_state = Interfaces::ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::getState / isHealthy
// ============================================================
Interfaces::ServiceState AuthManager::getState() const {
    return m_state;
}

bool AuthManager::isHealthy() const {
    return m_state == Interfaces::ServiceState::RUNNING &&
           Managers::UserManager::getInstance().isInitialized() &&
           SessionManager::getInstance().isHealthy();
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport AuthManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    auto sessionStats = SessionManager::getInstance().getStats();
    auto rlStats      = Security::RateLimiter::getInstance().getStats();

    report.status = isHealthy()
        ? Interfaces::HealthStatus::HEALTHY
        : Interfaces::HealthStatus::DEGRADED;

    snprintf(report.detail, sizeof(report.detail),
             "Sessions:%d/%d RateBlocked:%lu",
             static_cast<int>(sessionStats.currentActive),
             static_cast<int>(SessionManager::MAX_SESSIONS),
             static_cast<unsigned long>(rlStats.totalBlocked));

    return report;
}

// ============================================================
// IAuthProvider::authenticate
// ============================================================
Result AuthManager::authenticate(
    const char*                    username,
    const char*                    password,
    Interfaces::AuthenticatedUser& outUser)
{
    Models::User user;
    Result r = Managers::UserManager::getInstance()
                   .verifyCredentials(username, password, user);

    if (GW_ERR(r)) return r;

    // Fill output
    strncpy(outUser.userId,   user.userId,   sizeof(outUser.userId) - 1);
    strncpy(outUser.username, user.username, sizeof(outUser.username) - 1);
    outUser.role            = static_cast<Interfaces::UserRole>(user.role);
    outUser.authenticatedAt = static_cast<uint32_t>(millis() / 1000);
    outUser.valid           = true;

    return Result::OK;
}

// ============================================================
// IAuthProvider::validateSession
// ============================================================
Result AuthManager::validateSession(
    const char*                    sessionId,
    Interfaces::AuthenticatedUser& outUser)
{
    Models::Session session;
    Result r = SessionManager::getInstance()
                   .validateSession(sessionId, nullptr, session);

    if (GW_ERR(r)) return r;

    strncpy(outUser.userId,   session.userId,   sizeof(outUser.userId) - 1);
    strncpy(outUser.username, session.username, sizeof(outUser.username) - 1);
    outUser.role            = static_cast<Interfaces::UserRole>(session.role);
    outUser.authenticatedAt = session.createdAt;
    outUser.valid           = true;

    return Result::OK;
}

// ============================================================
// IAuthProvider::createSession
// ============================================================
Result AuthManager::createSession(
    const Interfaces::AuthenticatedUser& user,
    char*                                outSessionId,
    size_t                               sessionIdBufferSize)
{
    // Build Models::User from AuthenticatedUser for SessionManager
    Models::User modelUser;
    strncpy(modelUser.userId,   user.userId,   sizeof(modelUser.userId) - 1);
    strncpy(modelUser.username, user.username, sizeof(modelUser.username) - 1);
    modelUser.role = static_cast<Models::UserRole>(user.role);
    modelUser.updatePermissions();

    return SessionManager::getInstance().createSession(
        modelUser, nullptr, outSessionId, sessionIdBufferSize
    );
}

// ============================================================
// IAuthProvider::destroySession
// ============================================================
Result AuthManager::destroySession(const char* sessionId) {
    return SessionManager::getInstance().destroySession(sessionId);
}

// ============================================================
// IAuthProvider::hasPermission
// ============================================================
bool AuthManager::hasPermission(
    const Interfaces::AuthenticatedUser& user,
    const char*                          resource,
    const char*                          action) const
{
    // Map resource+action to Permission bitmask
    Models::UserRole role =
        static_cast<Models::UserRole>(user.role);

    Models::Permission perms = Models::permissionsForRole(role);

    // Resource-action mapping
    if (strcmp(action, "read") == 0) {
        if (strcmp(resource, "users") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::VIEW_USERS);
        }
        if (strcmp(resource, "devices") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::VIEW_DEVICES);
        }
        if (strcmp(resource, "system") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::VIEW_SYSTEM_STATUS);
        }
        if (strcmp(resource, "logs") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::VIEW_LOGS);
        }
    }

    if (strcmp(action, "write") == 0) {
        if (strcmp(resource, "users") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::CREATE_USER |
                Models::Permission::EDIT_USER);
        }
        if (strcmp(resource, "config") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::MANAGE_CONFIG);
        }
        if (strcmp(resource, "devices") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::MANAGE_DEVICES);
        }
    }

    if (strcmp(action, "delete") == 0) {
        if (strcmp(resource, "users") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::DELETE_USER);
        }
    }

    if (strcmp(action, "execute") == 0) {
        if (strcmp(resource, "restart") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::RESTART_SYSTEM);
        }
        if (strcmp(resource, "ota") == 0) {
            return Models::hasPermission(perms,
                Models::Permission::PERFORM_OTA);
        }
    }

    return false;
}

// ============================================================
// login — complete login flow
// ============================================================
Result AuthManager::login(
    const char* username,
    const char* password,
    const char* clientIP,
    char*       outSessionId,
    size_t      sessionIdBufferSize)
{
    if (!username || !password) return Result::ERR_NULL_POINTER;

    // Rate limit check
    auto policy = Security::RateLimitPolicy::login();
    Result r = Security::RateLimiter::getInstance()
                   .checkRequest(clientIP ? clientIP : "0.0.0.0",
                                  policy);

    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Login rate limited for IP: %s",
                 clientIP ? clientIP : "unknown");

        Services::EventBus::getInstance().publish(
            Interfaces::EventType::AUTH_LOGIN_FAILED,
            static_cast<uint32_t>(Result::ERR_RATE_LIMITED)
        );

        return Result::ERR_RATE_LIMITED;
    }

    // Authenticate
    Models::User user;
    r = Managers::UserManager::getInstance()
            .verifyCredentials(username, password, user);

    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Login failed for '%s' from %s: %s",
                 username,
                 clientIP ? clientIP : "unknown",
                 ResultHelper::toString(r));

        Services::EventBus::getInstance().publish(
            Interfaces::EventType::AUTH_LOGIN_FAILED,
            static_cast<uint32_t>(r)
        );

        return r;
    }

    // Record successful login
    Managers::UserManager::getInstance()
        .recordSuccessfulLogin(user.userId);

    // Create session
    r = SessionManager::getInstance().createSession(
        user, clientIP, outSessionId, sessionIdBufferSize
    );

    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Session creation failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    GW_LOG_I(TAG, "Login successful: user='%s' role='%s' ip=%s",
             user.username,
             Models::roleToString(user.role),
             clientIP ? clientIP : "unknown");

    return Result::OK;
}

// ============================================================
// logout
// ============================================================
Result AuthManager::logout(const char* sessionId,
                            const char* clientIP) {
    if (!sessionId) return Result::ERR_NULL_POINTER;

    Result r = SessionManager::getInstance().destroySession(sessionId);

    if (GW_OK(r)) {
        GW_LOG_I(TAG, "Logout from IP: %s",
                 clientIP ? clientIP : "unknown");
    }

    return r;
}

// ============================================================
// validateSessionFull
// ============================================================
Result AuthManager::validateSessionFull(
    const char*      sessionId,
    const char*      clientIP,
    Models::Session& outSession)
{
    return SessionManager::getInstance()
               .validateSession(sessionId, clientIP, outSession);
}

// ============================================================
// checkPermission
// ============================================================
bool AuthManager::checkPermission(
    const char*        sessionId,
    const char*        clientIP,
    Models::Permission required)
{
    Models::Session session;
    Result r = validateSessionFull(sessionId, clientIP, session);
    if (GW_ERR(r)) return false;

    return session.hasPermission(required);
}

// ============================================================
// checkRole
// ============================================================
bool AuthManager::checkRole(
    const char*      sessionId,
    const char*      clientIP,
    Models::UserRole minimumRole)
{
    Models::Session session;
    Result r = validateSessionFull(sessionId, clientIP, session);
    if (GW_ERR(r)) return false;

    return session.role >= minimumRole;
}

} // namespace Auth
} // namespace Gateway