// ============================================================
// UserManager.cpp
// ============================================================

#include "UserManager.h"
#include "../security/PBKDF2.h"
#include "../security/SecureRandom.h"
#include "../storage/StorageManager.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"

#include <Arduino.h>
#include <cstring>
#include <cstdio>
#include <cctype>

namespace Gateway {
namespace Managers {

static constexpr const char* TAG = "UserManager";

// ============================================================
// Singleton
// ============================================================
UserManager& UserManager::getInstance() noexcept {
    static UserManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
UserManager::UserManager()
    : m_initialized(false)
    , m_users{}
    , m_userCount(0)
    , m_mutex(nullptr)
{
}

// ============================================================
// IManager::initialize
// ============================================================
Result UserManager::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    // Attempt to load users from persistent storage
    Result r = load();

    if (GW_ERR(r) || m_userCount == 0) {
        GW_LOG_W(TAG, "No users found — creating default admin.");
        r = createDefaultAdmin();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "Failed to create default admin: %s",
                     ResultHelper::toString(r));
            return r;
        }
    }

    m_initialized = true;
    GW_LOG_I(TAG, "Initialized. Users: %d", static_cast<int>(m_userCount));
    return Result::OK;
}

// ============================================================
// IManager::isInitialized
// ============================================================
bool UserManager::isInitialized() const {
    return m_initialized;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport UserManager::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    if (!m_initialized) {
        report.status = Interfaces::HealthStatus::CRITICAL;
        snprintf(report.detail, sizeof(report.detail),
                 "Not initialized");
    } else if (getAdminCount() == 0) {
        report.status = Interfaces::HealthStatus::CRITICAL;
        snprintf(report.detail, sizeof(report.detail),
                 "No admin users! Users:%d", static_cast<int>(m_userCount));
    } else {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail),
                 "Users:%d Admins:%d",
                 static_cast<int>(m_userCount),
                 static_cast<int>(getAdminCount()));
    }

    return report;
}

// ============================================================
// nowSeconds
// ============================================================
uint32_t UserManager::nowSeconds() noexcept {
    return static_cast<uint32_t>(millis() / 1000UL);
}

// ============================================================
// generateUserId — 32 hex chars (128-bit)
// ============================================================
Result UserManager::generateUserId(char*  outBuffer,
                                    size_t bufferSize) {
    return Security::SecureRandom::getInstance()
               .generateHexToken(outBuffer, bufferSize, 16);
}

// ============================================================
// findUserById (mutable)
// ============================================================
Models::User* UserManager::findUserById(const char* userId) noexcept {
    if (!userId) return nullptr;
    for (uint8_t i = 0; i < m_userCount; ++i) {
        if (strncmp(m_users[i].userId, userId,
                     sizeof(m_users[i].userId) - 1) == 0) {
            return &m_users[i];
        }
    }
    return nullptr;
}

// ============================================================
// findUserById (const)
// ============================================================
const Models::User*
UserManager::findUserById(const char* userId) const noexcept {
    if (!userId) return nullptr;
    for (uint8_t i = 0; i < m_userCount; ++i) {
        if (strncmp(m_users[i].userId, userId,
                     sizeof(m_users[i].userId) - 1) == 0) {
            return &m_users[i];
        }
    }
    return nullptr;
}

// ============================================================
// findUserByUsername (mutable)
// ============================================================
Models::User*
UserManager::findUserByUsername(const char* username) noexcept {
    if (!username) return nullptr;
    for (uint8_t i = 0; i < m_userCount; ++i) {
        if (strncasecmp(m_users[i].username, username,
                         sizeof(m_users[i].username) - 1) == 0) {
            return &m_users[i];
        }
    }
    return nullptr;
}

// ============================================================
// findUserByUsername (const)
// ============================================================
const Models::User*
UserManager::findUserByUsername(const char* username) const noexcept {
    if (!username) return nullptr;
    for (uint8_t i = 0; i < m_userCount; ++i) {
        if (strncasecmp(m_users[i].username, username,
                         sizeof(m_users[i].username) - 1) - 1 == 0 ||
            strncasecmp(m_users[i].username, username,
                         sizeof(m_users[i].username) - 1) == 0) {
            return &m_users[i];
        }
    }
    return nullptr;
}

// ============================================================
// createDefaultAdmin
// ============================================================
Result UserManager::createDefaultAdmin() {
    Models::UserCreateRequest req;
    strncpy(req.username,
            Config::UserConfig::DEFAULT_ADMIN_USER,
            sizeof(req.username) - 1);

    // Default password: "Admin@1234"
    // MUST be changed on first login
    strncpy(req.password, "Admin@1234", sizeof(req.password) - 1);
    strncpy(req.displayName, "Administrator",
            sizeof(req.displayName) - 1);
    req.role = Models::UserRole::SUPERADMIN;

    char userId[33];
    Result r = createUser(req, userId, sizeof(userId));

    req.clearPassword();

    if (GW_OK(r)) {
        GW_LOG_W(TAG, "Default admin created. "
                 "Username: '%s' Password: 'Admin@1234' "
                 "CHANGE IMMEDIATELY!",
                 Config::UserConfig::DEFAULT_ADMIN_USER);
    }

    return r;
}

// ============================================================
// createUser
// ============================================================
Result UserManager::createUser(
    const Models::UserCreateRequest& request,
    char*                            outUserId,
    size_t                           userIdBufferSize)
{
    if (!outUserId)          return Result::ERR_NULL_POINTER;
    if (!request.username[0]) return Result::ERR_INVALID_ARGUMENT;
    if (!request.password[0]) return Result::ERR_INVALID_ARGUMENT;

    // Validate username length
    size_t ulen = strlen(request.username);
    if (ulen < Config::UserConfig::USERNAME_MIN_LEN ||
        ulen > Config::UserConfig::USERNAME_MAX_LEN) {
        return Result::ERR_USER_INVALID_USERNAME;
    }

    // Validate username characters (alphanumeric + _)
    for (size_t i = 0; i < ulen; ++i) {
        unsigned char c = static_cast<unsigned char>(request.username[i]);
        if (!isalnum(c) && c != '_' && c != '-') {
            return Result::ERR_USER_INVALID_USERNAME;
        }
    }

    // Validate password policy
    Result r = Security::PBKDF2::getInstance()
                   .validatePasswordPolicy(request.password);
    if (GW_ERR(r)) return r;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    // Check capacity
    if (m_userCount >= MAX_USERS) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_MAX_REACHED;
    }

    // Check uniqueness
    if (findUserByUsername(request.username) != nullptr) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_ALREADY_EXISTS;
    }

    // Build user record
    Models::User& user = m_users[m_userCount];
    user = Models::User{};

    // Generate userId
    r = generateUserId(user.userId, sizeof(user.userId));
    if (GW_ERR(r)) {
        xSemaphoreGive(m_mutex);
        return r;
    }

    // Copy username and display name
    strncpy(user.username, request.username,
            sizeof(user.username) - 1);

    if (request.displayName[0]) {
        strncpy(user.displayName, request.displayName,
                sizeof(user.displayName) - 1);
    } else {
        strncpy(user.displayName, request.username,
                sizeof(user.displayName) - 1);
    }

    // Hash password
    r = Security::PBKDF2::getInstance().hashPassword(
        request.password,
        user.passwordHash,
        sizeof(user.passwordHash)
    );

    if (GW_ERR(r)) {
        xSemaphoreGive(m_mutex);
        return r;
    }

    // Set role and permissions
    user.role        = request.role;
    user.updatePermissions();

    // Set timestamps
    user.createdAt           = nowSeconds();
    user.lastPasswordChange  = nowSeconds();
    user.isActive            = true;

    // Increment count
    m_userCount++;

    // Copy userId for caller
    if (outUserId && userIdBufferSize > 0) {
        strncpy(outUserId, user.userId, userIdBufferSize - 1);
        outUserId[userIdBufferSize - 1] = '\0';
    }

    xSemaphoreGive(m_mutex);

    // Persist
    r = save();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Failed to persist new user: %s",
                 ResultHelper::toString(r));
    }

    GW_LOG_I(TAG, "User created: '%s' [%s] ID:%s",
             user.username,
             Models::roleToString(user.role),
             user.userId);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::USER_CREATED,
        static_cast<uint32_t>(user.role)
    );

    return Result::OK;
}

// ============================================================
// getUserById
// ============================================================
Result UserManager::getUserById(const char*   userId,
                                 Models::User& outUser) const {
    if (!userId) return Result::ERR_NULL_POINTER;
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    const Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    outUser = *user;
    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// getUserByUsername
// ============================================================
Result UserManager::getUserByUsername(const char*   username,
                                       Models::User& outUser) const {
    if (!username) return Result::ERR_NULL_POINTER;
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    const Models::User* user = findUserByUsername(username);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    outUser = *user;
    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// updateUser
// ============================================================
Result UserManager::updateUser(
    const char*                      userId,
    const Models::UserUpdateRequest& request)
{
    if (!userId) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    if (request.updateDisplayName && request.displayName[0]) {
        strncpy(user->displayName, request.displayName,
                sizeof(user->displayName) - 1);
    }

    if (request.updateRole) {
        // Guard: cannot demote last admin
        if (user->role >= Models::UserRole::ADMIN &&
            request.role < Models::UserRole::ADMIN) {
            if (getAdminCount() <= 1) {
                xSemaphoreGive(m_mutex);
                return Result::ERR_USER_CANNOT_DELETE_LAST_ADMIN;
            }
        }
        user->role = request.role;
        user->updatePermissions();
    }

    if (request.updateActive) {
        if (!request.isActive && user->role >= Models::UserRole::ADMIN) {
            if (getAdminCount() <= 1) {
                xSemaphoreGive(m_mutex);
                return Result::ERR_USER_CANNOT_DELETE_LAST_ADMIN;
            }
        }
        user->isActive = request.isActive;
    }

    xSemaphoreGive(m_mutex);

    Result r = save();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Failed to save after update: %s",
                 ResultHelper::toString(r));
    }

    GW_LOG_I(TAG, "User updated: %s", userId);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::USER_UPDATED
    );

    return Result::OK;
}

// ============================================================
// changePassword
// ============================================================
Result UserManager::changePassword(
    const char*                          userId,
    const Models::PasswordChangeRequest& request)
{
    if (!userId) return Result::ERR_NULL_POINTER;

    // Validate new password policy
    Result r = Security::PBKDF2::getInstance()
                   .validatePasswordPolicy(request.newPassword);
    if (GW_ERR(r)) return r;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    // Verify current password
    bool match = false;
    r = Security::PBKDF2::getInstance().verifyPassword(
        request.currentPassword,
        user->passwordHash,
        match
    );

    if (GW_ERR(r) || !match) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_INVALID_CREDENTIALS;
    }

    // Hash new password
    char newHash[Security::PBKDF2::HASH_STRING_SIZE];
    r = Security::PBKDF2::getInstance().hashPassword(
        request.newPassword,
        newHash,
        sizeof(newHash)
    );

    if (GW_ERR(r)) {
        memset(newHash, 0, sizeof(newHash));
        xSemaphoreGive(m_mutex);
        return r;
    }

    memcpy(user->passwordHash, newHash, sizeof(newHash));
    memset(newHash, 0, sizeof(newHash));

    user->lastPasswordChange = nowSeconds();

    xSemaphoreGive(m_mutex);

    r = save();
    GW_LOG_I(TAG, "Password changed for user: %s", userId);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::USER_PASSWORD_CHANGED
    );

    return r;
}

// ============================================================
// adminResetPassword
// ============================================================
Result UserManager::adminResetPassword(
    const char* userId,
    const char* newPassword,
    const char* requestingAdminId)
{
    if (!userId || !newPassword || !requestingAdminId) {
        return Result::ERR_NULL_POINTER;
    }

    Result r = Security::PBKDF2::getInstance()
                   .validatePasswordPolicy(newPassword);
    if (GW_ERR(r)) return r;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    char newHash[Security::PBKDF2::HASH_STRING_SIZE];
    r = Security::PBKDF2::getInstance().hashPassword(
        newPassword, newHash, sizeof(newHash)
    );

    if (GW_ERR(r)) {
        memset(newHash, 0, sizeof(newHash));
        xSemaphoreGive(m_mutex);
        return r;
    }

    memcpy(user->passwordHash, newHash, sizeof(newHash));
    memset(newHash, 0, sizeof(newHash));

    user->lastPasswordChange = nowSeconds();

    xSemaphoreGive(m_mutex);

    GW_LOG_W(TAG, "Admin '%s' reset password for user '%s'",
             requestingAdminId, userId);

    return save();
}

// ============================================================
// deleteUser
// ============================================================
Result UserManager::deleteUser(const char* userId,
                                const char* requestingUserId) {
    if (!userId || !requestingUserId) return Result::ERR_NULL_POINTER;

    // Self-delete guard
    if (strncmp(userId, requestingUserId,
                 sizeof(Models::User::userId) - 1) == 0) {
        return Result::ERR_USER_CANNOT_DELETE_SELF;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    // Last admin guard
    if (user->role >= Models::UserRole::ADMIN &&
        getAdminCount() <= 1) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_CANNOT_DELETE_LAST_ADMIN;
    }

    // Find index and compact array
    uint8_t idx = static_cast<uint8_t>(user - m_users);

    GW_LOG_W(TAG, "Deleting user: '%s' [%s]",
             user->username, userId);

    // Sanitize deleted user
    m_users[idx].invalidate();

    // Compact
    for (uint8_t i = idx; i < m_userCount - 1; ++i) {
        m_users[i] = m_users[i + 1];
    }
    m_users[--m_userCount] = Models::User{};

    xSemaphoreGive(m_mutex);

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::USER_DELETED
    );

    return save();
}

// ============================================================
// verifyCredentials
// ============================================================
Result UserManager::verifyCredentials(
    const char*  username,
    const char*  password,
    Models::User& outUser)
{
    if (!username || !password) return Result::ERR_NULL_POINTER;
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserByUsername(username);

    if (!user) {
        xSemaphoreGive(m_mutex);
        // Perform dummy hash to prevent username enumeration via timing
        char dummy[Security::PBKDF2::HASH_STRING_SIZE] =
            "pbkdf2$1000$0000000000000000000000000000000"
            "0$00000000000000000000000000000000"
            "0000000000000000000000000000000000";
        bool match = false;
        Security::PBKDF2::getInstance()
            .verifyPassword(password, dummy, match);
        return Result::ERR_AUTH_INVALID_CREDENTIALS;
    }

    // Check account active
    if (!user->isActive) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_ACCOUNT_LOCKED;
    }

    // Check lockout
    if (user->isCurrentlyLocked(nowSeconds())) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_AUTH_ACCOUNT_LOCKED;
    }

    // Copy hash for verification outside mutex
    char hashCopy[Security::PBKDF2::HASH_STRING_SIZE];
    memcpy(hashCopy, user->passwordHash, sizeof(hashCopy));

    xSemaphoreGive(m_mutex);

    // Verify password (CPU-intensive — outside mutex)
    bool match = false;
    Result r = Security::PBKDF2::getInstance()
                   .verifyPassword(password, hashCopy, match);

    memset(hashCopy, 0, sizeof(hashCopy));

    if (GW_ERR(r)) return r;

    if (!match) {
        recordFailedLogin(username);
        return Result::ERR_AUTH_INVALID_CREDENTIALS;
    }

    // Success
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        outUser = *findUserByUsername(username);
        xSemaphoreGive(m_mutex);
    }

    return Result::OK;
}

// ============================================================
// recordFailedLogin
// ============================================================
Result UserManager::recordFailedLogin(const char* username) {
    if (!username) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserByUsername(username);
    if (user) {
        user->failedLoginCount++;

        if (user->failedLoginCount >= Config::Auth::LOGIN_MAX_ATTEMPTS) {
            user->isLocked    = true;
            user->lockoutUntil = nowSeconds() + Config::Auth::LOGIN_LOCKOUT_S;

            GW_LOG_W(TAG, "Account locked: '%s' (too many failures)",
                     username);

            Services::EventBus::getInstance().publish(
                Interfaces::EventType::AUTH_ACCOUNT_LOCKED
            );
        }
    }

    xSemaphoreGive(m_mutex);

    return user ? save() : Result::OK;
}

// ============================================================
// recordSuccessfulLogin
// ============================================================
Result UserManager::recordSuccessfulLogin(const char* userId) {
    if (!userId) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (user) {
        user->failedLoginCount = 0;
        user->isLocked         = false;
        user->lockoutUntil     = 0;
        user->lastLoginAt      = nowSeconds();
    }

    xSemaphoreGive(m_mutex);
    return user ? save() : Result::ERR_USER_NOT_FOUND;
}

// ============================================================
// isAccountLocked
// ============================================================
bool UserManager::isAccountLocked(const char* username) const {
    if (!username) return false;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return false;
    }

    const Models::User* user = findUserByUsername(username);
    bool locked = user ? user->isCurrentlyLocked(nowSeconds()) : false;

    xSemaphoreGive(m_mutex);
    return locked;
}

// ============================================================
// unlockAccount
// ============================================================
Result UserManager::unlockAccount(const char* userId) {
    if (!userId) return Result::ERR_NULL_POINTER;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Models::User* user = findUserById(userId);
    if (!user) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_USER_NOT_FOUND;
    }

    user->isLocked         = false;
    user->lockoutUntil     = 0;
    user->failedLoginCount = 0;

    xSemaphoreGive(m_mutex);

    GW_LOG_I(TAG, "Account unlocked: %s", userId);
    return save();
}

// ============================================================
// listUsers
// ============================================================
uint8_t UserManager::listUsers(
    Models::PublicUserInfo* outBuffer,
    uint8_t                 maxCount) const
{
    if (!outBuffer || maxCount == 0) return 0;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return 0;
    }

    uint8_t count = (m_userCount < maxCount) ? m_userCount : maxCount;
    for (uint8_t i = 0; i < count; ++i) {
        outBuffer[i] = Models::PublicUserInfo(m_users[i]);
    }

    xSemaphoreGive(m_mutex);
    return count;
}

// ============================================================
// getUserCount / getAdminCount / userExists
// ============================================================
uint8_t UserManager::getUserCount() const noexcept {
    return m_userCount;
}

uint8_t UserManager::getAdminCount() const noexcept {
    uint8_t count = 0;
    for (uint8_t i = 0; i < m_userCount; ++i) {
        if (m_users[i].isActive &&
            m_users[i].role >= Models::UserRole::ADMIN) {
            count++;
        }
    }
    return count;
}

bool UserManager::userExists(const char* username) const noexcept {
    return findUserByUsername(username) != nullptr;
}

// ============================================================
// save — persist to LittleFS JSON
// ============================================================
Result UserManager::save() const {
    JsonDocument doc;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result r = serializeToJson(doc);
    xSemaphoreGive(m_mutex);

    if (GW_ERR(r)) return r;

    char jsonBuffer[JSON_DOC_SIZE];
    size_t written = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

    if (written == 0 || written >= sizeof(jsonBuffer)) {
        return Result::ERR_USER_SAVE_FAILED;
    }

    r = Storage::StorageManager::getInstance().writeJsonFile(
        Config::Storage::FS_USERS_FILE, jsonBuffer
    );

    if (GW_OK(r)) {
        GW_LOG_D(TAG, "Users saved (%d users, %zu bytes)",
                 static_cast<int>(m_userCount), written);
    }

    return r;
}

// ============================================================
// load — read from LittleFS JSON
// ============================================================
Result UserManager::load() {
    bool exists = false;
    Storage::StorageManager::getInstance().fileExists(
        Config::Storage::FS_USERS_FILE, exists
    );

    if (!exists) {
        GW_LOG_I(TAG, "Users file not found.");
        return Result::ERR_FILE_NOT_FOUND;
    }

    char   jsonBuffer[JSON_DOC_SIZE];
    size_t jsonSize = 0;

    Result r = Storage::StorageManager::getInstance().readJsonFile(
        Config::Storage::FS_USERS_FILE,
        jsonBuffer,
        sizeof(jsonBuffer),
        jsonSize
    );

    if (GW_ERR(r)) return r;
    if (jsonSize == 0) return Result::ERR_USER_LOAD_FAILED;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, jsonBuffer, jsonSize
    );

    if (err) {
        GW_LOG_E(TAG, "JSON parse error: %s", err.c_str());
        return Result::ERR_USER_LOAD_FAILED;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    r = deserializeFromJson(doc);
    xSemaphoreGive(m_mutex);

    if (GW_OK(r)) {
        GW_LOG_I(TAG, "Loaded %d users from storage.",
                 static_cast<int>(m_userCount));
    }

    return r;
}

// ============================================================
// serializeToJson
// ============================================================
Result UserManager::serializeToJson(JsonDocument& doc) const {
    JsonArray users = doc["users"].to<JsonArray>();

    for (uint8_t i = 0; i < m_userCount; ++i) {
        JsonObject obj = users.add<JsonObject>();
        serializeUser(obj, m_users[i]);
    }

    doc["count"] = m_userCount;
    return Result::OK;
}

// ============================================================
// deserializeFromJson
// ============================================================
Result UserManager::deserializeFromJson(const JsonDocument& doc) {
    m_userCount = 0;

    if (!doc["users"].is<JsonArray>()) {
        return Result::ERR_USER_LOAD_FAILED;
    }

    JsonArrayConst users = doc["users"].as<JsonArrayConst>();

    for (JsonObjectConst obj : users) {
        if (m_userCount >= MAX_USERS) break;

        Models::User& user = m_users[m_userCount];
        user = Models::User{};

        Result r = deserializeUser(obj, user);
        if (GW_OK(r) && user.isValid()) {
            m_userCount++;
        }
    }

    return Result::OK;
}

// ============================================================
// serializeUser
// ============================================================
Result UserManager::serializeUser(JsonObject&         obj,
                                   const Models::User& user) const {
    obj["id"]            = user.userId;
    obj["username"]      = user.username;
    obj["displayName"]   = user.displayName;
    obj["passwordHash"]  = user.passwordHash;
    obj["role"]          = Models::roleToString(user.role);
    obj["isActive"]      = user.isActive;
    obj["isLocked"]      = user.isLocked;
    obj["failedLogins"]  = user.failedLoginCount;
    obj["lockoutUntil"]  = user.lockoutUntil;
    obj["createdAt"]     = user.createdAt;
    obj["lastLoginAt"]   = user.lastLoginAt;
    obj["lastPwdChange"] = user.lastPasswordChange;
    obj["createdBy"]     = user.createdBy;
    return Result::OK;
}

// ============================================================
// deserializeUser
// ============================================================
Result UserManager::deserializeUser(const JsonObject& obj,
                                     Models::User&     user) {
    if (!obj["id"].is<const char*>()) return Result::ERR_INVALID_ARGUMENT;

    strncpy(user.userId,
            obj["id"].as<const char*>(),
            sizeof(user.userId) - 1);

    strncpy(user.username,
            obj["username"] | "",
            sizeof(user.username) - 1);

    strncpy(user.displayName,
            obj["displayName"] | user.username,
            sizeof(user.displayName) - 1);

    strncpy(user.passwordHash,
            obj["passwordHash"] | "",
            sizeof(user.passwordHash) - 1);

    user.role = Models::roleFromString(
        obj["role"] | "viewer"
    );

    user.isActive          = obj["isActive"]     | true;
    user.isLocked          = obj["isLocked"]     | false;
    user.failedLoginCount  = obj["failedLogins"] | 0;
    user.lockoutUntil      = obj["lockoutUntil"] | 0;
    user.createdAt         = obj["createdAt"]    | 0;
    user.lastLoginAt       = obj["lastLoginAt"]  | 0;
    user.lastPasswordChange= obj["lastPwdChange"]| 0;

    strncpy(user.createdBy,
            obj["createdBy"] | "",
            sizeof(user.createdBy) - 1);

    user.sanitize();
    user.updatePermissions();

    return Result::OK;
}

} // namespace Managers
} // namespace Gateway