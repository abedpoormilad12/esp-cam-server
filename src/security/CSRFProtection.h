// ============================================================
// CSRFProtection.h
// Cross-Site Request Forgery protection.
//
// Design decisions:
//   - Synchronizer Token Pattern (most robust CSRF defense)
//   - Per-session CSRF token (not per-request, to allow
//     multiple browser tabs without invalidating each other)
//   - Token stored server-side in Session object
//   - Token delivered to client in:
//       a) Hidden form field for HTML forms
//       b) JSON response body for AJAX
//   - Client must echo token in:
//       a) X-CSRF-Token header (AJAX/fetch)
//       b) _csrf body field (HTML forms)
//   - Constant-time comparison to prevent timing attacks
//   - Token is 32 bytes (256-bit) of secure random
//   - Token rotation: optional on privilege escalation
//
// Exemptions (do not require CSRF token):
//   - GET, HEAD, OPTIONS requests (safe methods)
//   - Requests with valid API key (non-browser clients)
//   - WebSocket upgrade (protected by WS handshake)
// ============================================================

#pragma once

#ifndef CSRF_PROTECTION_H
#define CSRF_PROTECTION_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Security {

class CSRFProtection final {
public:
    // Token buffer size: 64 hex chars + null terminator
    static constexpr size_t TOKEN_BUFFER_SIZE =
        Config::Auth::CSRF_TOKEN_BYTES * 2 + 1;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static CSRFProtection& getInstance() noexcept;

    // --------------------------------------------------------
    // Generate a new CSRF token.
    // outToken must be TOKEN_BUFFER_SIZE bytes.
    // --------------------------------------------------------
    [[nodiscard]] Result generateToken(char*  outToken,
                                       size_t bufferSize);

    // --------------------------------------------------------
    // Validate a submitted token against the session token.
    // Both parameters are null-terminated hex strings.
    // Uses constant-time comparison.
    // --------------------------------------------------------
    [[nodiscard]] Result validateToken(const char* submittedToken,
                                       const char* sessionToken,
                                       bool&       outValid);

    // --------------------------------------------------------
    // Check if an HTTP method requires CSRF validation.
    // GET, HEAD, OPTIONS are exempt (safe methods per RFC 7231)
    // --------------------------------------------------------
    [[nodiscard]] static bool methodRequiresCSRF(
        const char* httpMethod
    ) noexcept;

    // --------------------------------------------------------
    // Extract CSRF token from request.
    // Checks X-CSRF-Token header first, then _csrf body field.
    // Returns nullptr if not found.
    // outBuffer must be TOKEN_BUFFER_SIZE bytes.
    // --------------------------------------------------------
    [[nodiscard]] Result extractToken(
        const char* headerValue,   // X-CSRF-Token header (may be nullptr)
        const char* bodyValue,     // _csrf form field   (may be nullptr)
        char*       outBuffer,
        size_t      bufferSize
    );

private:
    CSRFProtection();
    ~CSRFProtection() = default;

    CSRFProtection(const CSRFProtection&)            = delete;
    CSRFProtection& operator=(const CSRFProtection&) = delete;

    // Constant-time string comparison
    [[nodiscard]] static bool constantTimeStrCompare(
        const char* a,
        const char* b,
        size_t      maxLen
    ) noexcept;
};

} // namespace Security
} // namespace Gateway

#endif // CSRF_PROTECTION_H