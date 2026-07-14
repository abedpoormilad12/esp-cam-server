// ============================================================
// SessionManager.cpp
// ============================================================

#include "SessionManager.h"
#include "../security/SecureRandom.h"
#include "../security/CSRFProtection.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Auth {

static constexpr const char* TAG             = "SessionManager";
static constexpr uint32_t    CLEANUP_INTERVAL_MS = 60000; // 1 min

// ============================================================
// Singleton
// ============================================================
SessionManager& SessionManager::getInstance() noexcept {
    static SessionManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
SessionManager::SessionManager()
    : m_state(Interfaces::ServiceState::UNINITIALIZED)
    , m_sessions{}
    , m_mutex(nullptr)
    , m_lastCleanupMs(0)
    , m_statsMutex(nullptr)
    , m_stats{}
{
}

// ============================================================
// nowSeconds
// ============================================================
uint32_t SessionManager::nowSeconds() noexcept {
    return static_cast<uint32_t>(millis() / 1000UL);
}

// ============================================================
// IService::initialize
// ============================================================
Result SessionManager::initialize() {
    if (m_state != ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    m_statsMutex = xSemaphoreCreateMutex();
    if (!m_statsMutex) return Result::ERR_OUT_OF_MEMORY;

    // Initialize all sessions as invalid
    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        m_sessions[i] = Models::Session{};
    }

    m_stats = Stats{};
    m_state = ServiceState::STOPPED;

    GW_LOG_I(TAG, "Initialized. Pool: %d sessions.", MAX_SESSIONS);
    return Result::OK;
}

// ============================================================
// IService::start / stop
// ============================================================
Result SessionManager::start() {
    if (m_state != ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }
    m_state = ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started.");
    return Result::OK;
}

Result SessionManager::stop() {
    if (m_state == ServiceState::RUNNING) {
        destroyAllSessions();
        m_state = ServiceState::STOPPED;
    }
    return Result::OK;
}

// ============================================================
// IService::tick — called from a periodic context
// ============================================================
void SessionManager::tick() {
    uint32_t now = static_cast<uint32_t>(millis());
    if (now - m_lastCleanupMs >= CLEANUP_INTERVAL_MS) {
        m_lastCleanupMs = now;
        uint8_t expired = cleanupExpiredSessions();
        if (expired > 0) {
            GW_LOG_D(TAG, "Cleanup: %d expired sessions removed.",
                     static_cast<int>(expired));
        }
    }
}

// ============================================================
// IService::isHealthy
// ============================================================
bool SessionManager::isHealthy() const {
    return m_state == ServiceState::RUNNING;
}

// ============================================================
// IService::getState
// ============================================================
ServiceState SessionManager::getState() const {
    return m_state;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport SessionManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    Stats s = getStats();
    report.status = isHealthy()
        ? Interfaces::HealthStatus::HEALTHY
        : Interfaces::HealthStatus::DEGRADED;

    snprintf(report.detail, sizeof(report.detail),
             "Active:%d/%d Created:%lu Expired:%lu",
             static_cast<int>(s.currentActive),
             static_cast<int>(MAX_SESSIONS),
             static_cast<unsigned long>(s.totalCreated),
             static_cast<unsigned long>(s.totalExpired));

    return report;
}

// ============================================================
// findSession (mutable)
// ============================================================
Models::Session* SessionManager::findSession(const char* sid) {
    if (!sid || sid[0] == '\0') return nullptr;

    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].valid &&
            strncmp(m_sessions[i].sessionId, sid,
                     sizeof(Models::Session::sessionId) - 1) == 0) {
            return &m_sessions[i];
        }
    }
    return nullptr;
}

// ============================================================
// findSession (const)
// ============================================================
const Models::Session*
SessionManager::findSession(const char* sid) const {
    if (!sid || sid[0] == '\0') return nullptr;

    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].valid &&
            strncmp(m_sessions[i].sessionId, sid,
                     sizeof(Models::Session::sessionId) - 1) == 0) {
            return &m_sessions[i];
        }
    }
    return nullptr;
}

// ============================================================
// findFreeSlot — first invalid slot
// ============================================================
Models::Session* SessionManager::findFreeSlot() {
    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (!m_sessions[i].valid) return &m_sessions[i];
    }
    return nullptr;
}

// ============================================================
// findLRUSlot — least recently used (for eviction)
// ============================================================
Models::Session* SessionManager::findLRUSlot() {
    Models::Session* lru     = &m_sessions[0];
    uint32_t         oldest  = m_sessions[0].lastAccessAt;

    for (uint8_t i = 1; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].lastAccessAt < oldest) {
            oldest = m_sessions[i].lastAccessAt;
            lru    = &m_sessions[i];
        }
    }

    return lru;
}

// ============================================================
// createSession
// ============================================================
Result SessionManager::createSession(
    const Models::User& user,
    const char*         clientIP,
    char*               outSessionId,
    size_t              sessionIdBufferSize)
{
    if (!outSessionId)  return Result::ERR_NULL_POINTER;
    if (!user.isValid()) return Result::ERR_INVALID_ARGUMENT;

    if (sessionIdBufferSize < sizeof(Models::Session::sessionId)) {
        return Result::ERR_BUFFER_TOO_SMALL;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    // Find a slot (free or LRU eviction)
    Models::Session* slot = findFreeSlot();
    if (!slot) {
        GW_LOG_W(TAG, "Session pool full — evicting LRU session.");
        slot = findLRUSlot();
        slot->invalidate();
    }

    // Generate session ID
    Result r = Security::SecureRandom::getInstance()
                   .generateSessionId(slot->sessionId,
                                       sizeof(slot->sessionId));
    if (GW_ERR(r)) {
        xSemaphoreGive(m_mutex);
        return r;
    }

    // Generate CSRF token
    r = Security::CSRFProtection::getInstance()
            .generateToken(slot->csrfToken, sizeof(slot->csrfToken));
    if (GW_ERR(r)) {
        xSemaphoreGive(m_mutex);
        return r;
    }

    // Populate session
    strncpy(slot->userId,   user.userId,   sizeof(slot->userId) - 1);
    strncpy(slot->username, user.username, sizeof(slot->username) - 1);
    slot->role        = user.role;
    slot->permissions = user.permissions;

    if (clientIP) {
        strncpy(slot->boundIP, clientIP, sizeof(slot->boundIP) - 1);
        slot->boundIP[sizeof(slot->boundIP) - 1] = '\0';
    }

    uint32_t now        = nowSeconds();
    slot->createdAt     = now;
    slot->lastAccessAt  = now;
    slot->expiresAt     = now + Config::Auth::SESSION_ABSOLUTE_MAX_S;
    slot->valid         = true;

    // Copy session ID to caller
    strncpy(outSessionId, slot->sessionId, sessionIdBufferSize - 1);
    outSessionId[sessionIdBufferSize - 1] = '\0';

    // Update stats
    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalCreated++;
        m_stats.currentActive = getActiveSessionCount();
        xSemaphoreGive(m_statsMutex);
    }

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "Session created for user '%s' from IP %s",
             user.username, clientIP ? clientIP : "unknown");

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::AUTH_LOGIN_SUCCESS
    );

    return Result::OK;
}

// ============================================================
// validateSession
// ============================================================
Result SessionManager::validateSession(
    const char*      sessionId,
    const char*      clientIP,
    Models::Session& outSession)
{
    if (!sessionId || sessionId[0] == '\0') {
        return Result::ERR_AUTH_SESSION_INVALID;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::Session* session = findSession(sessionId);

    if (!session) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_SESSION_INVALID;
    }

    uint32_t now = nowSeconds();

    // Check expiry
    if (session->isExpired(now)) {
        session->invalidate();
        xSemaphoreGive(m_mutex);

        if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
            m_stats.totalExpired++;
            xSemaphoreGive(m_statsMutex);
        }

        GW_LOG_D(TAG, "Session expired.");
        return Result::ERR_AUTH_SESSION_EXPIRED;
    }

    // IP binding check
    if (clientIP && !session->isIPMatch(clientIP)) {
        GW_LOG_W(TAG, "Session IP mismatch! Session:%s Request:%s",
                 session->boundIP, clientIP);

        if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
            m_stats.totalRejectedIP++;
            xSemaphoreGive(m_statsMutex);
        }

        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_SESSION_INVALID;
    }

    // Touch session
    session->touch(now);
    outSession = *session;

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// destroySession
// ============================================================
Result SessionManager::destroySession(const char* sessionId) {
    if (!sessionId) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::Session* session = findSession(sessionId);
    if (!session) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_SESSION_INVALID;
    }

    char username[33];
    strncpy(username, session->username, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';

    session->invalidate();

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalDestroyed++;
        m_stats.currentActive = getActiveSessionCount();
        xSemaphoreGive(m_statsMutex);
    }

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "Session destroyed for user '%s'", username);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::AUTH_LOGOUT
    );

    return Result::OK;
}

// ============================================================
// destroyUserSessions
// ============================================================
Result SessionManager::destroyUserSessions(const char* userId) {
    if (!userId) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].valid &&
            strncmp(m_sessions[i].userId, userId,
                     sizeof(Models::Session::userId) - 1) == 0) {
            m_sessions[i].invalidate();
            count++;
        }
    }

    xSemaphoreGive(m_mutex);

    if (count > 0) {
        GW_LOG_I(TAG, "Destroyed %d sessions for user %s",
                 static_cast<int>(count), userId);
    }

    return Result::OK;
}

// ============================================================
// destroyAllSessions
// ============================================================
Result SessionManager::destroyAllSessions() {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        m_sessions[i].invalidate();
    }

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "All sessions destroyed.");
    return Result::OK;
}

// ============================================================
// cleanupExpiredSessions
// ============================================================
uint8_t SessionManager::cleanupExpiredSessions() {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return 0;
    }

    uint8_t  count  = 0;
    uint32_t now    = nowSeconds();

    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].valid && m_sessions[i].isExpired(now)) {
            m_sessions[i].invalidate();
            count++;
        }
    }

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalExpired  += count;
        m_stats.currentActive  = getActiveSessionCount();
        xSemaphoreGive(m_statsMutex);
    }

    xSemaphoreGive(m_mutex);
    return count;
}

// ============================================================
// getActiveSessionCount
// ============================================================
uint8_t SessionManager::getActiveSessionCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_SESSIONS; ++i) {
        if (m_sessions[i].valid) count++;
    }
    return count;
}

// ============================================================
// sessionExists
// ============================================================
bool SessionManager::sessionExists(const char* sessionId) const {
    if (!sessionId) return false;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }

    bool exists = findSession(sessionId) != nullptr;
    xSemaphoreGive(m_mutex);
    return exists;
}

// ============================================================
// getCSRFToken
// ============================================================
Result SessionManager::getCSRFToken(
    const char* sessionId,
    char*       outToken,
    size_t      tokenBufferSize) const
{
    if (!sessionId || !outToken) return Result::ERR_NULL_POINTER;

    if (tokenBufferSize < sizeof(Models::Session::csrfToken)) {
        return Result::ERR_BUFFER_TOO_SMALL;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    const Models::Session* session = findSession(sessionId);
    if (!session) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_SESSION_INVALID;
    }

    strncpy(outToken, session->csrfToken, tokenBufferSize - 1);
    outToken[tokenBufferSize - 1] = '\0';

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// getStats
// ============================================================
SessionManager::Stats SessionManager::getStats() const noexcept {
    Stats copy{};
    if (xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_statsMutex);
    }
    copy.currentActive = getActiveSessionCount();
    return copy;
}

} // namespace Auth
} // namespace Gateway