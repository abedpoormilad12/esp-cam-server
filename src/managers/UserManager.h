// ============================================================
// UserManager.h
// User lifecycle management with persistent storage.
//
// Design decisions:
//   - In-memory user table (max 10 users per SystemConfig)
//     loaded from LittleFS JSON at boot
//   - PBKDF2 password hashing — never store plaintext
//   - UUID-style userId generated via SecureRandom
//   - Thread-safe: RW mutex (concurrent reads, write lock)
//   - Atomic JSON write (temp file + rename via LittleFSStorage)
//   - Admin guard: cannot delete last admin account
//   - Self-guard: cannot delete own account via API
//   - Username uniqueness enforced
//   - Audit: every mutating operation logged
//   - Default admin created on first boot if no users exist
// ============================================================

#pragma once

#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include "../interfaces/IManager.h"
#include "../interfaces/IHealthCheck.h"
#include "../models/User.h"
#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Managers {

class UserManager final
    : public Interfaces::IManager
    , public Interfaces::IHealthCheck
{
public:
    static constexpr uint8_t  MAX_USERS     = Config::UserConfig::MAX_USERS;
    static constexpr size_t   JSON_DOC_SIZE = 4096;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static UserManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IManager
    // --------------------------------------------------------
    [[nodiscard]] Result      initialize()    override;
    [[nodiscard]] bool        isInitialized() const override;
    [[nodiscard]] const char* getName()       const override { return "UserManager"; }

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "UserManager"; }

    // --------------------------------------------------------
    // User CRUD
    // --------------------------------------------------------

    // Create a new user. Returns OK and fills outUserId.
    [[nodiscard]] Result createUser(
        const Models::UserCreateRequest& request,
        char*                            outUserId,
        size_t                           userIdBufferSize
    );

    // Get user by ID (full record)
    [[nodiscard]] Result getUserById(
        const char*  userId,
        Models::User& outUser
    ) const;

    // Get user by username
    [[nodiscard]] Result getUserByUsername(
        const char*  username,
        Models::User& outUser
    ) const;

    // Update user fields
    [[nodiscard]] Result updateUser(
        const char*                       userId,
        const Models::UserUpdateRequest&  request
    );

    // Change password (requires current password verification)
    [[nodiscard]] Result changePassword(
        const char*                          userId,
        const Models::PasswordChangeRequest& request
    );

    // Admin: reset password without current password
    [[nodiscard]] Result adminResetPassword(
        const char* userId,
        const char* newPassword,
        const char* requestingAdminId
    );

    // Delete user (soft: marks inactive; hard: removes from table)
    [[nodiscard]] Result deleteUser(
        const char* userId,
        const char* requestingUserId   // for self-delete guard
    );

    // --------------------------------------------------------
    // Authentication support
    // --------------------------------------------------------

    // Verify password and return user if valid
    [[nodiscard]] Result verifyCredentials(
        const char*  username,
        const char*  password,
        Models::User& outUser
    );

    // Record a failed login attempt
    [[nodiscard]] Result recordFailedLogin(const char* username);

    // Record a successful login
    [[nodiscard]] Result recordSuccessfulLogin(const char* userId);

    // Check if account is locked
    [[nodiscard]] bool isAccountLocked(const char* username) const;

    // Unlock account (admin action)
    [[nodiscard]] Result unlockAccount(const char* userId);

    // --------------------------------------------------------
    // List / Query
    // --------------------------------------------------------

    // Fill outBuffer with up to maxCount public user infos
    [[nodiscard]] uint8_t listUsers(
        Models::PublicUserInfo* outBuffer,
        uint8_t                 maxCount
    ) const;

    [[nodiscard]] uint8_t getUserCount()      const noexcept;
    [[nodiscard]] uint8_t getAdminCount()     const noexcept;
    [[nodiscard]] bool    userExists(const char* username) const noexcept;

    // --------------------------------------------------------
    // Persistence
    // --------------------------------------------------------
    [[nodiscard]] Result save() const;
    [[nodiscard]] Result load();

private:
    UserManager();
    ~UserManager() = default;

    UserManager(const UserManager&)            = delete;
    UserManager& operator=(const UserManager&) = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    Result createDefaultAdmin();
    Result generateUserId(char* outBuffer, size_t bufferSize);

    Models::User*       findUserById(const char* userId) noexcept;
    const Models::User* findUserById(const char* userId) const noexcept;
    Models::User*       findUserByUsername(const char* username) noexcept;
    const Models::User* findUserByUsername(const char* username) const noexcept;

    Result serializeToJson(JsonDocument& doc) const;
    Result deserializeFromJson(const JsonDocument& doc);
    Result serializeUser(JsonObject& obj,
                          const Models::User& user) const;
    Result deserializeUser(
        const JsonObjectConst& obj,
        Models::User& user
    ) const;

    static uint32_t nowSeconds() noexcept;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool              m_initialized;
    Models::User      m_users[MAX_USERS];
    uint8_t           m_userCount;
    SemaphoreHandle_t m_mutex;
};

} // namespace Managers
} // namespace Gateway

#endif // USER_MANAGER_H