// ============================================================
// HttpContext.h
// Per-request context object passed through middleware chain.
//
// Design decisions:
//   - Single object carries all request/response state
//   - Avoids repeated string parsing across middleware layers
//   - Session and auth data cached here after first validation
//   - Fixed-size buffers: no heap allocation per request
//   - Response builder pattern: accumulate headers + body
//     then send once
//   - clientIP extracted once at entry point
// ============================================================

#pragma once

#ifndef HTTP_CONTEXT_H
#define HTTP_CONTEXT_H

#include "../models/Session.h"
#include "../models/UserRole.h"
#include "../core/ErrorCodes.h"

#include <ESPAsyncWebServer.h>
#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Web {

// ============================================================
// HttpContext
// ============================================================
struct HttpContext {
    // ---- Raw request (borrowed pointers — valid for request lifetime) ----
    AsyncWebServerRequest* request;

    // ---- Extracted request data (owned copies) ----
    char     clientIP[16];
    char     sessionId[65];     // from cookie
    char     csrfToken[65];     // from header or body
    char     method[8];         // "GET", "POST", etc.
    char     uri[128];

    // ---- Auth state (populated by AuthMiddleware) ----
    bool               authenticated;
    Models::Session    session;

    // ---- Middleware control ----
    bool               handled;     // true = stop chain, response sent
    bool               aborted;     // true = error response sent

    // ---- Request body (for POST/PUT) ----
    const char*        bodyData;    // points to request body
    size_t             bodyLength;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
explicit HttpContext(AsyncWebServerRequest* req)
    : request(req)
    , clientIP{}
    , sessionId{}
    , csrfToken{}
    , method{}
    , uri{}
    , authenticated(false)
    , session{}
    , handled(false)
    , aborted(false)
    , bodyData(nullptr)
    , bodyLength(0)
{
    if (req) {
        // Extract client IP
        IPAddress ip = req->client()->remoteIP();
        snprintf(clientIP, sizeof(clientIP), "%d.%d.%d.%d",
                 ip[0], ip[1], ip[2], ip[3]);

        // Extract method
        strncpy(method, req->methodToString(),
                sizeof(method) - 1);
        method[sizeof(method) - 1] = '\0';

        // Extract URI
        strncpy(uri, req->url().c_str(),
                sizeof(uri) - 1);
        uri[sizeof(uri) - 1] = '\0';

        // Extract session cookie (manual parse)
        if (req->hasHeader("Cookie")) {
            String cookieHeader = req->header("Cookie");
            String needle = String(Config::Auth::SESSION_COOKIE_NAME) + "=";
            int idx = cookieHeader.indexOf(needle);

            if (idx != -1) {
                int start = idx + needle.length();
                int end   = cookieHeader.indexOf(';', start);
                if (end == -1) end = cookieHeader.length();

                String cookieValue = cookieHeader.substring(start, end);
                cookieValue.trim();

                strncpy(sessionId, cookieValue.c_str(),
                        sizeof(sessionId) - 1);
                sessionId[sizeof(sessionId) - 1] = '\0';
            }
        }

        // Extract CSRF token from header
        if (req->hasHeader(Config::Auth::CSRF_HEADER_NAME)) {
            String csrf = req->getHeader(
                Config::Auth::CSRF_HEADER_NAME
            )->value();
            strncpy(csrfToken, csrf.c_str(),
                    sizeof(csrfToken) - 1);
            csrfToken[sizeof(csrfToken) - 1] = '\0';
        }
    }
}
    // --------------------------------------------------------
    // Convenience accessors
    // --------------------------------------------------------
    [[nodiscard]] bool isAuthenticated() const noexcept {
        return authenticated;
    }

    [[nodiscard]] bool hasRole(Models::UserRole minRole) const noexcept {
        return authenticated && session.role >= minRole;
    }

    [[nodiscard]] bool hasPermission(Models::Permission p) const noexcept {
        return authenticated && session.hasPermission(p);
    }

    [[nodiscard]] const char* getUsername() const noexcept {
        return authenticated ? session.username : "anonymous";
    }

    [[nodiscard]] const char* getUserId() const noexcept {
        return authenticated ? session.userId : "";
    }

    [[nodiscard]] Models::UserRole getRole() const noexcept {
        return authenticated ? session.role : Models::UserRole::NONE;
    }
};

} // namespace Web
} // namespace Gateway

#endif // HTTP_CONTEXT_H