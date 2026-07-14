// ============================================================
// HttpResponse.h
// Response builder with security headers baked in.
//
// Design decisions:
//   - All responses include mandatory security headers
//   - JSON responses use fixed-size stack buffer
//   - Cookie management: secure, HttpOnly, SameSite
//   - Content-Type always set explicitly
//   - Cache-Control appropriate per content type
//   - No raw string concatenation: snprintf everywhere
// ============================================================

#pragma once

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include "../core/SystemConfig.h"

#include <ESPAsyncWebServer.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Gateway {
namespace Web {

// ============================================================
// HTTP status codes
// ============================================================
namespace HttpStatus {
    static constexpr int OK                    = 200;
    static constexpr int CREATED               = 201;
    static constexpr int NO_CONTENT            = 204;
    static constexpr int MOVED_PERMANENTLY     = 301;
    static constexpr int NOT_MODIFIED          = 304;
    static constexpr int BAD_REQUEST           = 400;
    static constexpr int UNAUTHORIZED          = 401;
    static constexpr int FORBIDDEN             = 403;
    static constexpr int NOT_FOUND             = 404;
    static constexpr int METHOD_NOT_ALLOWED    = 405;
    static constexpr int CONFLICT              = 409;
    static constexpr int PAYLOAD_TOO_LARGE     = 413;
    static constexpr int UNPROCESSABLE_ENTITY  = 422;
    static constexpr int TOO_MANY_REQUESTS     = 429;
    static constexpr int INTERNAL_SERVER_ERROR = 500;
    static constexpr int SERVICE_UNAVAILABLE   = 503;
}

// ============================================================
// Content types
// ============================================================
namespace ContentType {
    static constexpr const char* JSON       = "application/json";
    static constexpr const char* HTML       = "text/html; charset=utf-8";
    static constexpr const char* CSS        = "text/css";
    static constexpr const char* JS         = "application/javascript";
    static constexpr const char* PLAIN      = "text/plain";
    static constexpr const char* ICO        = "image/x-icon";
    static constexpr const char* PNG        = "image/png";
    static constexpr const char* SVG        = "image/svg+xml";
}

// ============================================================
// HttpResponse — static helper methods
// ============================================================
class HttpResponse {
public:
    // --------------------------------------------------------
    // Security headers added to every response
    // --------------------------------------------------------
    static void addSecurityHeaders(AsyncWebServerResponse* response) {
        if (!response) return;

        response->addHeader("X-Content-Type-Options",  "nosniff");
        response->addHeader("X-Frame-Options",          "DENY");
        response->addHeader("X-XSS-Protection",         "1; mode=block");
        response->addHeader("Referrer-Policy",
                            "strict-origin-when-cross-origin");
        response->addHeader("Content-Security-Policy",
                            "default-src 'self'; "
                            "script-src 'self'; "
                            "style-src 'self' 'unsafe-inline'; "
                            "img-src 'self' data:; "
                            "connect-src 'self' ws:; "
                            "frame-ancestors 'none'");
        response->addHeader("Permissions-Policy",
                            "geolocation=(), camera=(), microphone=()");
    }

    // --------------------------------------------------------
    // Send JSON response
    // --------------------------------------------------------
    static void sendJSON(AsyncWebServerRequest* req,
                          int                    statusCode,
                          const char*            jsonBody) {
        if (!req || !jsonBody) return;

        AsyncWebServerResponse* resp = req->beginResponse(
            statusCode,
            ContentType::JSON,
            jsonBody
        );

        addSecurityHeaders(resp);
        resp->addHeader("Cache-Control",
                        "no-store, no-cache, must-revalidate");
        resp->addHeader("Pragma", "no-cache");

        req->send(resp);
    }

    // --------------------------------------------------------
    // Send simple JSON with message field
    // --------------------------------------------------------
    static void sendMessage(AsyncWebServerRequest* req,
                             int                    statusCode,
                             const char*            message) {
        if (!req || !message) return;

        char body[256];
        bool success = (statusCode >= 200 && statusCode < 300);
        snprintf(body, sizeof(body),
                 "{\"success\":%s,\"message\":\"%s\"}",
                 success ? "true" : "false",
                 message);

        sendJSON(req, statusCode, body);
    }

    // --------------------------------------------------------
    // Send error response
    // --------------------------------------------------------
    static void sendError(AsyncWebServerRequest* req,
                           int                    statusCode,
                           const char*            error,
                           const char*            detail = nullptr) {
        if (!req) return;

        char body[256];
        if (detail) {
            snprintf(body, sizeof(body),
                     "{\"success\":false,\"error\":\"%s\","
                     "\"detail\":\"%s\"}",
                     error, detail);
        } else {
            snprintf(body, sizeof(body),
                     "{\"success\":false,\"error\":\"%s\"}",
                     error);
        }

        sendJSON(req, statusCode, body);
    }

    // --------------------------------------------------------
    // Send HTML response
    // --------------------------------------------------------
    static void sendHTML(AsyncWebServerRequest* req,
                          int                    statusCode,
                          const char*            html) {
        if (!req || !html) return;

        AsyncWebServerResponse* resp = req->beginResponse(
            statusCode, ContentType::HTML, html
        );

        addSecurityHeaders(resp);
        resp->addHeader("Cache-Control",
                        "no-store, no-cache, must-revalidate");

        req->send(resp);
    }

    // --------------------------------------------------------
    // Send redirect
    // --------------------------------------------------------
    static void sendRedirect(AsyncWebServerRequest* req,
                               const char*            location,
                               int                    statusCode = 302) {
        if (!req || !location) return;

        AsyncWebServerResponse* resp =
            req->beginResponse(statusCode);

        resp->addHeader("Location", location);
        addSecurityHeaders(resp);

        req->send(resp);
    }

    // --------------------------------------------------------
    // Set session cookie
    // --------------------------------------------------------
    static void setSessionCookie(AsyncWebServerResponse* resp,
                                  const char*             sessionId,
                                  uint32_t                maxAgeSeconds) {
        if (!resp || !sessionId) return;

        char cookieValue[256];

        // Build cookie string with security attributes
        snprintf(cookieValue, sizeof(cookieValue),
                 "%s=%s; Max-Age=%lu; Path=/; HttpOnly; "
                 "SameSite=Strict%s",
                 Config::Auth::SESSION_COOKIE_NAME,
                 sessionId,
                 static_cast<unsigned long>(maxAgeSeconds),
                 Config::Security::SECURE_COOKIE_FLAG ? "; Secure" : "");

        resp->addHeader("Set-Cookie", cookieValue);
    }

    // --------------------------------------------------------
    // Clear session cookie (logout)
    // --------------------------------------------------------
    static void clearSessionCookie(AsyncWebServerResponse* resp) {
        if (!resp) return;

        char cookieValue[128];
        snprintf(cookieValue, sizeof(cookieValue),
                 "%s=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict",
                 Config::Auth::SESSION_COOKIE_NAME);

        resp->addHeader("Set-Cookie", cookieValue);
    }

    // --------------------------------------------------------
    // Send 401 Unauthorized (redirects for browser, JSON for API)
    // --------------------------------------------------------
    static void sendUnauthorized(AsyncWebServerRequest* req,
                                  bool                   isApiRequest) {
        if (isApiRequest) {
            sendError(req, HttpStatus::UNAUTHORIZED,
                      "Unauthorized", "Authentication required");
        } else {
            sendRedirect(req, "/index.html");
        }
    }

    // --------------------------------------------------------
    // Send 403 Forbidden
    // --------------------------------------------------------
    static void sendForbidden(AsyncWebServerRequest* req) {
        sendError(req, HttpStatus::FORBIDDEN,
                  "Forbidden", "Insufficient permissions");
    }

    // --------------------------------------------------------
    // Detect if request is API (expects JSON) or browser
    // --------------------------------------------------------
    static bool isApiRequest(AsyncWebServerRequest* req) {
        if (!req) return false;

        // Check Accept header
        if (req->hasHeader("Accept")) {
            String accept = req->getHeader("Accept")->value();
            if (accept.indexOf("application/json") >= 0) {
                return true;
            }
        }

        // Check URL prefix
        if (req->url().startsWith("/api/")) {
            return true;
        }

        return false;
    }

private:
    HttpResponse()  = delete;
    ~HttpResponse() = delete;
};

} // namespace Web
} // namespace Gateway

#endif // HTTP_RESPONSE_H