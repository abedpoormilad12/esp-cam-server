// ============================================================
// AuthHandler.cpp
// ============================================================

#include "AuthHandler.h"
#include "../../auth/AuthManager.h"
#include "../../auth/SessionManager.h"
#include "../HttpResponse.h"
#include "../../services/Logger.h"
#include "../../core/SystemConfig.h"

#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "AuthHandler";

AuthHandler& AuthHandler::getInstance() noexcept {
    static AuthHandler instance;
    return instance;
}

// ============================================================
// POST /api/auth/login
// Body: { "username": "...", "password": "..." }
// ============================================================
void AuthHandler::handleLogin(HttpContext& ctx,
                               const char* body,
                               size_t      len) {
    if (!body || len == 0) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::BAD_REQUEST,
                                 "BadRequest",
                                 "Empty body");
        return;
    }

    // Parse JSON body
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, len);

    if (err) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::BAD_REQUEST,
                                 "BadRequest",
                                 "Invalid JSON");
        return;
    }

    const char* username = doc["username"] | "";
    const char* password = doc["password"] | "";

    if (!username[0] || !password[0]) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::BAD_REQUEST,
                                 "BadRequest",
                                 "Missing credentials");
        return;
    }

    // Perform login
    char sessionId[65];
    Result r = Auth::AuthManager::getInstance().login(
        username,
        password,
        ctx.clientIP,
        sessionId,
        sizeof(sessionId)
    );

    if (r == Result::ERR_RATE_LIMITED) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::TOO_MANY_REQUESTS,
                                 "RateLimited",
                                 "Too many attempts");
        return;
    }

    if (r == Result::ERR_AUTH_ACCOUNT_LOCKED) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::FORBIDDEN,
                                 "AccountLocked",
                                 "Account is locked");
        return;
    }

    if (GW_ERR(r)) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::UNAUTHORIZED,
                                 "InvalidCredentials",
                                 "Wrong username or password");
        return;
    }

    // Get CSRF token for this session
    char csrfToken[65];
    Auth::SessionManager::getInstance().getCSRFToken(
        sessionId, csrfToken, sizeof(csrfToken)
    );

    // Get session for user info
    Models::Session session;
    Auth::SessionManager::getInstance().validateSession(
        sessionId, ctx.clientIP, session
    );

    // Build response
    char responseBody[512];
    snprintf(responseBody, sizeof(responseBody),
             "{\"success\":true,"
             "\"csrfToken\":\"%s\","
             "\"user\":{"
             "\"username\":\"%s\","
             "\"role\":\"%s\","
             "\"displayName\":\"%s\""
             "}}",
             csrfToken,
             session.username,
             Models::roleToString(session.role),
             session.username);

    // Build response with cookie
    AsyncWebServerResponse* resp = ctx.request->beginResponse(
        HttpStatus::OK, ContentType::JSON, responseBody
    );

    HttpResponse::setSessionCookie(
        resp, sessionId,
        Config::Auth::SESSION_TIMEOUT_S
    );
    HttpResponse::addSecurityHeaders(resp);
    resp->addHeader("Cache-Control",
                    "no-store, no-cache, must-revalidate");

    ctx.request->send(resp);
    ctx.handled = true;

    GW_LOG_I(TAG, "Login success: '%s' from %s",
             username, ctx.clientIP);
}

// ============================================================
// POST /api/auth/logout
// ============================================================
void AuthHandler::handleLogout(HttpContext& ctx) {
    if (ctx.sessionId[0] != '\0') {
        Auth::AuthManager::getInstance().logout(
            ctx.sessionId, ctx.clientIP
        );
    }

    AsyncWebServerResponse* resp =
        ctx.request->beginResponse(
            HttpStatus::OK,
            ContentType::JSON,
            "{\"success\":true,\"message\":\"Logged out\"}"
        );

    HttpResponse::clearSessionCookie(resp);
    HttpResponse::addSecurityHeaders(resp);
    resp->addHeader("Cache-Control",
                    "no-store, no-cache, must-revalidate");

    ctx.request->send(resp);
    ctx.handled = true;
}

// ============================================================
// GET /api/auth/me
// ============================================================
void AuthHandler::handleMe(HttpContext& ctx) {
    if (!ctx.authenticated) {
        HttpResponse::sendUnauthorized(ctx.request, true);
        return;
    }

    char body[256];
    snprintf(body, sizeof(body),
             "{\"success\":true,"
             "\"user\":{"
             "\"userId\":\"%s\","
             "\"username\":\"%s\","
             "\"role\":\"%s\","
             "\"permissions\":%lu"
             "}}",
             ctx.session.userId,
             ctx.session.username,
             Models::roleToString(ctx.session.role),
             static_cast<unsigned long>(ctx.session.permissions));

    HttpResponse::sendJSON(ctx.request, HttpStatus::OK, body);
}

// ============================================================
// GET /api/auth/csrf
// ============================================================
void AuthHandler::handleGetCSRF(HttpContext& ctx) {
    if (!ctx.authenticated) {
        HttpResponse::sendUnauthorized(ctx.request, true);
        return;
    }

    char csrfToken[65];
    Result r = Auth::SessionManager::getInstance().getCSRFToken(
        ctx.sessionId, csrfToken, sizeof(csrfToken)
    );

    if (GW_ERR(r)) {
        HttpResponse::sendError(ctx.request,
                                 HttpStatus::INTERNAL_SERVER_ERROR,
                                 "TokenError", nullptr);
        return;
    }

    char body[128];
    snprintf(body, sizeof(body),
             "{\"success\":true,\"csrfToken\":\"%s\"}",
             csrfToken);

    HttpResponse::sendJSON(ctx.request, HttpStatus::OK, body);
}

} // namespace Web
} // namespace Gateway