// ============================================================
// CSRFMiddleware.h
// Validates CSRF token for state-changing requests.
// ============================================================

#pragma once

#ifndef CSRF_MIDDLEWARE_H
#define CSRF_MIDDLEWARE_H

#include "IMiddleware.h"

namespace Gateway {
namespace Web {

class CSRFMiddleware final : public IMiddleware {
public:
    CSRFMiddleware() = default;

    [[nodiscard]] bool        process(HttpContext& ctx) override;
    [[nodiscard]] const char* getName()  const override {
        return "CSRFMiddleware";
    }
};

} // namespace Web
} // namespace Gateway

#endif // CSRF_MIDDLEWARE_H