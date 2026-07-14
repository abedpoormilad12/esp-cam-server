// ============================================================
// RateLimitMiddleware.cpp
// ============================================================

#include "RateLimitMiddleware.h"
#include "../HttpResponse.h"
#include "../../services/Logger.h"
#include "../../config/ConfigManager.h"

#include <cstdio>

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "RateLimitMiddleware";

RateLimitMiddleware::RateLimitMiddleware(
    Security::RateLimitPolicy policy)
    : m_policy(policy)
{}

bool RateLimitMiddleware::process(HttpContext& ctx) {
    if (!Config::ConfigManager::getInstance().isRateLimitEnabled()) {
        return true;
    }

    Result r = Security::RateLimiter::getInstance()
                   .checkRequest(ctx.clientIP, m_policy);

    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "Rate limit exceeded for IP: %s", ctx.clientIP);

        uint8_t remaining = Security::RateLimiter::getInstance()
                                .getRemainingRequests(ctx.clientIP,
                                                       m_policy);

        AsyncWebServerResponse* resp =
            ctx.request->beginResponse(
                HttpStatus::TOO_MANY_REQUESTS,
                "application/json",
                "{\"success\":false,"
                "\"error\":\"Too Many Requests\"}"
            );

        HttpResponse::addSecurityHeaders(resp);

        char retryAfter[16];
        snprintf(retryAfter, sizeof(retryAfter), "%lu",
                 static_cast<unsigned long>(m_policy.lockoutSeconds));
        resp->addHeader("Retry-After", retryAfter);
        resp->addHeader("X-RateLimit-Remaining", "0");

        ctx.request->send(resp);
        ctx.handled = true;
        return false;
    }

    return true;
}

} // namespace Web
} // namespace Gateway