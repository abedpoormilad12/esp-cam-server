// ============================================================
// AuthMiddleware.h
// Validates session cookie and populates HttpContext auth data.
// ============================================================

#pragma once

#ifndef AUTH_MIDDLEWARE_H
#define AUTH_MIDDLEWARE_H

#include "IMiddleware.h"

namespace Gateway {
namespace Web {

class AuthMiddleware final : public IMiddleware {
public:
    // requireAuth: if true, sends 401 when not authenticated
    // if false, populates ctx.authenticated but continues
    explicit AuthMiddleware(bool requireAuth = false);

    [[nodiscard]] bool        process(HttpContext& ctx) override;
    [[nodiscard]] const char* getName()  const override {
        return "AuthMiddleware";
    }

private:
    bool m_requireAuth;
};

} // namespace Web
} // namespace Gateway

#endif // AUTH_MIDDLEWARE_H