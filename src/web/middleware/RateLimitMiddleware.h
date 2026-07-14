// ============================================================
// RateLimitMiddleware.h
// Per-IP rate limiting for all requests.
// ============================================================

#pragma once

#ifndef RATE_LIMIT_MIDDLEWARE_H
#define RATE_LIMIT_MIDDLEWARE_H

#include "IMiddleware.h"
#include "../../security/RateLimiter.h"

namespace Gateway {
namespace Web {

class RateLimitMiddleware final : public IMiddleware {
public:
    explicit RateLimitMiddleware(
        Security::RateLimitPolicy policy =
            Security::RateLimitPolicy::general()
    );

    [[nodiscard]] bool        process(HttpContext& ctx) override;
    [[nodiscard]] const char* getName()  const override {
        return "RateLimitMiddleware";
    }

private:
    Security::RateLimitPolicy m_policy;
};

} // namespace Web
} // namespace Gateway

#endif // RATE_LIMIT_MIDDLEWARE_H