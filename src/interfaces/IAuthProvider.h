// ============================================================
// IAuthProvider.h
// Authentication provider interface for the gateway.
// ============================================================

#pragma once

#ifndef IAUTH_PROVIDER_H
#define IAUTH_PROVIDER_H

#include "../core/ErrorCodes.h"
#include "../models/UserRole.h"
#include <cstddef>
#include <cstdint>

namespace Gateway {
namespace Interfaces {

using UserRole = Gateway::Models::UserRole;

struct AuthenticatedUser {
    char userId[32];
    char username[32];
    UserRole role;
    uint32_t authenticatedAt;
    bool valid;

    AuthenticatedUser()
        : role(UserRole::NONE)
        , authenticatedAt(0)
        , valid(false)
    {
        userId[0] = '\0';
        username[0] = '\0';
    }
};

class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;

    [[nodiscard]] virtual Result authenticate(
        const char* username,
        const char* password,
        AuthenticatedUser& outUser
    ) = 0;

    [[nodiscard]] virtual Result validateSession(
        const char* sessionId,
        AuthenticatedUser& outUser
    ) = 0;

    [[nodiscard]] virtual Result createSession(
        const AuthenticatedUser& user,
        char* outSessionId,
        size_t sessionIdBufferSize
    ) = 0;

    [[nodiscard]] virtual Result destroySession(
        const char* sessionId
    ) = 0;

    [[nodiscard]] virtual bool hasPermission(
        const AuthenticatedUser& user,
        const char* resource,
        const char* action
    ) const = 0;

protected:
    IAuthProvider() = default;

    IAuthProvider(const IAuthProvider&) = delete;
    IAuthProvider& operator=(const IAuthProvider&) = delete;
};

} // namespace Interfaces
} // namespace Gateway

#endif // IAUTH_PROVIDER_H