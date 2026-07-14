// ============================================================
// AuthMiddleware.cpp
// ============================================================

#include "AuthMiddleware.h"
#include "../../auth/AuthManager.h"
#include "../HttpResponse.h"
#include "../../services/Logger.h"

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "AuthMiddleware";

AuthMiddleware::AuthMiddleware(bool requireAuth)
    : m_requireAuth(requireAuth)
{}

bool AuthMiddleware::process(HttpContext& ctx) {
    // No session cookie present
    if (ctx.sessionId[0] == '\0') {
        ctx.authenticated = false;

        if (m_requireAuth) {
            bool isApi = HttpResponse::isApiRequest(ctx.request);
            HttpResponse::sendUnauthorized(ctx.request, isApi);
            ctx.handled = true;
            return false;
        }

        return true;  // Continue without auth
    }

    // Validate session
    Auth::AuthManager& auth = Auth::AuthManager::getInstance();
    Result r = auth.validateSessionFull(
        ctx.sessionId,
        ctx.clientIP,
        ctx.session
    );

    if (GW_ERR(r)) {
        ctx.authenticated = false;

        if (m_requireAuth) {
            if (r == Result::ERR_AUTH_SESSION_EXPIRED) {
                // Clear stale cookie
                AsyncWebServerResponse* resp =
                    ctx.request->beginResponse(302);
                HttpResponse::clearSessionCookie(resp);
                resp->addHeader("Location", "/index.html");
                HttpResponse::addSecurityHeaders(resp);
                ctx.request->send(resp);
            } else {
                HttpResponse::sendUnauthorized(
                    ctx.request,
                    HttpResponse::isApiRequest(ctx.request)
                );
            }
            ctx.handled = true;
            return false;
        }

        return true;
    }

    ctx.authenticated = true;
    return true;
}

} // namespace Web
} // namespace Gateway