// ============================================================
// AuthHandler.h
// Handles /api/auth/* endpoints.
// ============================================================

#pragma once

#ifndef AUTH_HANDLER_H
#define AUTH_HANDLER_H

#include "../HttpContext.h"

namespace Gateway {
namespace Web {

class AuthHandler final {
public:
    static AuthHandler& getInstance() noexcept;

    // POST /api/auth/login
    void handleLogin(HttpContext& ctx, const char* body, size_t len);

    // POST /api/auth/logout
    void handleLogout(HttpContext& ctx);

    // GET  /api/auth/me
    void handleMe(HttpContext& ctx);

    // GET  /api/auth/csrf
    void handleGetCSRF(HttpContext& ctx);

private:
    AuthHandler()  = default;
    ~AuthHandler() = default;

    AuthHandler(const AuthHandler&)            = delete;
    AuthHandler& operator=(const AuthHandler&) = delete;
};

} // namespace Web
} // namespace Gateway

#endif // AUTH_HANDLER_H