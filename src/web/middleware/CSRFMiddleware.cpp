// ============================================================
// CSRFMiddleware.cpp
// ============================================================

#include "CSRFMiddleware.h"
#include "../../security/CSRFProtection.h"
#include "../../auth/SessionManager.h"
#include "../HttpResponse.h"
#include "../../services/Logger.h"
#include "../../config/ConfigManager.h"

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "CSRFMiddleware";

bool CSRFMiddleware::process(HttpContext& ctx) {
    // Only check state-changing methods
    if (!Security::CSRFProtection::methodRequiresCSRF(ctx.method)) {
        return true;
    }

    // Skip if CSRF protection disabled in config
    if (!Config::ConfigManager::getInstance().isCSRFEnabled()) {
        return true;
    }

    // Must be authenticated to have a CSRF token
    if (!ctx.authenticated || ctx.sessionId[0] == '\0') {
        HttpResponse::sendError(
            ctx.request,
            HttpStatus::FORBIDDEN,
            "CSRF",
            "No session"
        );
        ctx.handled = true;
        return false;
    }

    // Get session CSRF token
    char sessionCSRF[65];
    Result r = Auth::SessionManager::getInstance().getCSRFToken(
        ctx.sessionId,
        sessionCSRF,
        sizeof(sessionCSRF)
    );

    if (GW_ERR(r)) {
        HttpResponse::sendError(
            ctx.request,
            HttpStatus::FORBIDDEN,
            "CSRF",
            "No token"
        );
        ctx.handled = true;
        return false;
    }

    // Validate submitted token
    bool valid = false;
    Security::CSRFProtection::getInstance().validateToken(
        ctx.csrfToken,
        sessionCSRF,
        valid
    );

    if (!valid) {
        GW_LOG_W(TAG, "CSRF validation failed for %s from %s",
                 ctx.uri, ctx.clientIP);

        HttpResponse::sendError(
            ctx.request,
            HttpStatus::FORBIDDEN,
            "CSRF",
            "Invalid token"
        );
        ctx.handled = true;
        return false;
    }

    return true;
}

} // namespace Web
} // namespace Gateway