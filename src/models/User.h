// ============================================================
// User.h
// User model — in-memory representation of a system user.
//
// Design decisions:
//   - Fixed-size fields: no heap allocation
//   - Password hash stored as formatted PBKDF2 string
//     (never store plaintext)
//   - userId: UUID-style unique identifier (hex string)
//   - createdAt / lastLoginAt: Unix timestamps (seconds)
//   - failedLoginCount: for lockout enforcement
//   - isActive flag: soft delete without losing audit trail
//   - Serializable to/from JSON via ArduinoJson
//   - copyable: needed for session storage
// ============================================================

#pragma once

#ifndef USER_H
#define USER_H

#include "UserRole.h"
#include "../security/PBKDF2.h"
#include "../core/ErrorCodes.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace Gateway {
namespace Models {

// ============================================================
// User
// ============================================================
struct User {
    // ---- Identity ----
    char     userId[33];        // 32 hex chars + null
    char     username[33];      // max 32 chars + null
    char     displayName[33];   // optional friendly name

    // ---- Security ----
    // Format: "pbkdf2$ITER$SALT_HEX$KEY_HEX"
    char     passwordHash[Security::PBKDF2::HASH_STRING_SIZE];

    // ---- Authorization ----
    UserRole role;
    uint32_t permissions;       // cached bitmask from role

    // ---- State ----
    bool     isActive;
    bool     isLocked;
    uint8_t  failedLoginCount;
    uint32_t lockoutUntil;      // Unix timestamp

    // ---- Timestamps (seconds since epoch or millis/1000) ----
    uint32_t createdAt;
    uint32_t lastLoginAt;
    uint32_t lastPasswordChange;

    // ---- Metadata ----
    char     createdBy[33];     // userId of creator

    // --------------------------------------------------------
    // Constructor — zero-initialize everything
    // --------------------------------------------------------
    User()
        : userId{}
        , username{}
        , displayName{}
        , passwordHash{}
        , role(UserRole::VIEWER)
        , permissions(0)
        , isActive(true)
        , isLocked(false)
        , failedLoginCount(0)
        , lockoutUntil(0)
        , createdAt(0)
        , lastLoginAt(0)
        , lastPasswordChange(0)
        , createdBy{}
    {
        permissions = static_cast<uint32_t>(
            permissionsForRole(role)
        );
    }

    // --------------------------------------------------------
    // Helpers
    // --------------------------------------------------------
    [[nodiscard]] bool isValid() const noexcept {
        return userId[0] != '\0' && username[0] != '\0';
    }

    [[nodiscard]] bool hasPermission(Permission p) const noexcept {
        return Models::hasPermission(
            static_cast<Permission>(permissions),
            p
        );
    }

    void updatePermissions() noexcept {
        permissions = static_cast<uint32_t>(
            permissionsForRole(role)
        );
    }

    [[nodiscard]] bool isCurrentlyLocked(uint32_t nowSeconds) const noexcept {
        return isLocked && (lockoutUntil == 0 || nowSeconds < lockoutUntil);
    }

    void sanitize() noexcept {
        // Ensure null termination on all string fields
        userId[sizeof(userId)-1]               = '\0';
        username[sizeof(username)-1]           = '\0';
        displayName[sizeof(displayName)-1]     = '\0';
        passwordHash[sizeof(passwordHash)-1]   = '\0';
        createdBy[sizeof(createdBy)-1]         = '\0';
    }
};

// Size assertion — must fit in reasonable RAM
static_assert(sizeof(User) <= 400,
              "User struct too large — review field sizes");

// ============================================================
// UserCreateRequest — input DTO for user creation
// ============================================================
struct UserCreateRequest {
    char     username[33];
    char     password[65];      // plaintext — only at creation
    char     displayName[33];
    UserRole role;

    UserCreateRequest()
        : username{}
        , password{}
        , displayName{}
        , role(UserRole::VIEWER)
    {}

    // Sanitize password from memory after use
    void clearPassword() noexcept {
        memset(password, 0, sizeof(password));
    }
};

// ============================================================
// UserUpdateRequest — input DTO for updates
// ============================================================
struct UserUpdateRequest {
    char     displayName[33];
    UserRole role;
    bool     isActive;
    bool     updateRole;
    bool     updateActive;
    bool     updateDisplayName;

    UserUpdateRequest()
        : displayName{}
        , role(UserRole::VIEWER)
        , isActive(true)
        , updateRole(false)
        , updateActive(false)
        , updateDisplayName(false)
    {}
};

// ============================================================
// PasswordChangeRequest
// ============================================================
struct PasswordChangeRequest {
    char currentPassword[65];
    char newPassword[65];

    PasswordChangeRequest() : currentPassword{}, newPassword{} {}

    void clear() noexcept {
        memset(currentPassword, 0, sizeof(currentPassword));
        memset(newPassword,     0, sizeof(newPassword));
    }
};

// ============================================================
// PublicUserInfo — safe to send to client (no hash)
// ============================================================
struct PublicUserInfo {
    char     userId[33];
    char     username[33];
    char     displayName[33];
    char     role[12];
    bool     isActive;
    bool     isLocked;
    uint32_t createdAt;
    uint32_t lastLoginAt;

    PublicUserInfo() = default;

    explicit PublicUserInfo(const User& u) {
        strncpy(userId,      u.userId,      sizeof(userId) - 1);
        strncpy(username,    u.username,    sizeof(username) - 1);
        strncpy(displayName, u.displayName, sizeof(displayName) - 1);
        strncpy(role, roleToString(u.role), sizeof(role) - 1);
        isActive    = u.isActive;
        isLocked    = u.isLocked;
        createdAt   = u.createdAt;
        lastLoginAt = u.lastLoginAt;
        // Null terminate
        userId[sizeof(userId)-1]           = '\0';
        username[sizeof(username)-1]       = '\0';
        displayName[sizeof(displayName)-1] = '\0';
        role[sizeof(role)-1]               = '\0';
    }
};

} // namespace Models
} // namespace Gateway

#endif // USER_H