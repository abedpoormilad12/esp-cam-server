// ============================================================
// Session.h
// Session model — server-side session entry.
//
// Design decisions:
//   - Fixed-size struct for static pool allocation
//   - Session ID: 64 hex chars (256-bit entropy)
//   - CSRF token: per-session, 64 hex chars
//   - IP binding: session bound to originating IP
//     (prevents session hijacking across networks)
//   - Absolute expiry: prevents infinite session extension
//   - Idle timeout: separate from absolute expiry
//   - User info cached in session for fast auth checks
//     without hitting UserManager on every request
// ============================================================

#pragma once

#ifndef SESSION_H
#define SESSION_H

#include "UserRole.h"
#include "../core/SystemConfig.h"

#include <cstdint>
#include <cstring>

namespace Gateway {
namespace Models {

// ============================================================
// Session
// ============================================================
struct Session {
    // Session identifier (cookie value)
    char     sessionId[65];     // 64 hex + null

    // CSRF protection token
    char     csrfToken[65];     // 64 hex + null

    // Cached user info (avoids UserManager lookup per request)
    char     userId[33];
    char     username[33];
    UserRole role;
    uint32_t permissions;

    // IP binding (dot-decimal)
    char     boundIP[16];

    // Timestamps (millis() / 1000 for seconds)
    uint32_t createdAt;         // Session creation time
    uint32_t lastAccessAt;      // Last request time (for idle timeout)
    uint32_t expiresAt;         // Absolute expiry

    // Validity flag
    bool     valid;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    Session()
        : sessionId{}
        , csrfToken{}
        , userId{}
        , username{}
        , role(UserRole::NONE)
        , permissions(0)
        , boundIP{}
        , createdAt(0)
        , lastAccessAt(0)
        , expiresAt(0)
        , valid(false)
    {}

    // --------------------------------------------------------
    // Helpers
    // --------------------------------------------------------
    void invalidate() noexcept {
        valid         = false;
        // Sanitize sensitive data
        memset(sessionId, 0, sizeof(sessionId));
        memset(csrfToken, 0, sizeof(csrfToken));
        memset(userId,    0, sizeof(userId));
        memset(username,  0, sizeof(username));
        role        = UserRole::NONE;
        permissions = 0;
    }

    [[nodiscard]] bool isExpired(uint32_t nowSeconds) const noexcept {
        if (!valid) return true;
        if (nowSeconds >= expiresAt) return true;
        // Idle timeout check
        uint32_t idleSeconds = nowSeconds - lastAccessAt;
        if (idleSeconds > Config::Auth::SESSION_TIMEOUT_S) return true;
        return false;
    }

    [[nodiscard]] bool isIPMatch(const char* ip) const noexcept {
        if (!ip || boundIP[0] == '\0') return true; // No binding
        return strncmp(boundIP, ip, sizeof(boundIP) - 1) == 0;
    }

    void touch(uint32_t nowSeconds) noexcept {
        lastAccessAt = nowSeconds;
    }

    [[nodiscard]] bool hasPermission(
        Models::Permission p) const noexcept
    {
        return Models::hasPermission(
            static_cast<Models::Permission>(permissions), p
        );
    }
};

// Size check
static_assert(sizeof(Session) <= 320,
              "Session struct too large for static pool");

} // namespace Models
} // namespace Gateway

#endif // SESSION_H