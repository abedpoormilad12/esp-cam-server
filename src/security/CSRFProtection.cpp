// ============================================================
// CSRFProtection.cpp
// ============================================================

#include "CSRFProtection.h"
#include "SecureRandom.h"

#include <cstring>
#include <cctype>

namespace Gateway {
namespace Security {

// ============================================================
// Singleton
// ============================================================
CSRFProtection& CSRFProtection::getInstance() noexcept {
    static CSRFProtection instance;
    return instance;
}

CSRFProtection::CSRFProtection() = default;

// ============================================================
// generateToken
// ============================================================
Result CSRFProtection::generateToken(char*  outToken,
                                      size_t bufferSize) {
    if (!outToken)                      return Result::ERR_NULL_POINTER;
    if (bufferSize < TOKEN_BUFFER_SIZE) return Result::ERR_BUFFER_TOO_SMALL;

    return SecureRandom::getInstance().generateCSRFToken(
        outToken,
        bufferSize
    );
}

// ============================================================
// validateToken
// ============================================================
Result CSRFProtection::validateToken(const char* submittedToken,
                                      const char* sessionToken,
                                      bool&       outValid) {
    outValid = false;

    if (!submittedToken || !sessionToken) {
        return Result::ERR_NULL_POINTER;
    }

    // Sanity check lengths — both must be exactly TOKEN_BUFFER_SIZE-1
    size_t submittedLen = strlen(submittedToken);
    size_t sessionLen   = strlen(sessionToken);

    // If lengths differ, tokens cannot match.
    // We still do a constant-time comparison to prevent
    // length-based timing oracle.
    if (submittedLen != TOKEN_BUFFER_SIZE - 1 ||
        sessionLen   != TOKEN_BUFFER_SIZE - 1) {
        // Force comparison anyway for constant time behavior
        constantTimeStrCompare(submittedToken, sessionToken,
                                TOKEN_BUFFER_SIZE - 1);
        outValid = false;
        return Result::OK;
    }

    outValid = constantTimeStrCompare(
        submittedToken,
        sessionToken,
        TOKEN_BUFFER_SIZE - 1
    );

    return Result::OK;
}

// ============================================================
// methodRequiresCSRF
// ============================================================
bool CSRFProtection::methodRequiresCSRF(
    const char* httpMethod) noexcept
{
    if (!httpMethod) return false;

    // Safe HTTP methods per RFC 7231 Section 4.2.1
    // These methods MUST NOT change server state, so no CSRF needed
    if (strcmp(httpMethod, "GET")     == 0) return false;
    if (strcmp(httpMethod, "HEAD")    == 0) return false;
    if (strcmp(httpMethod, "OPTIONS") == 0) return false;

    // All state-changing methods require CSRF protection
    return true;
}

// ============================================================
// extractToken
// Priority: X-CSRF-Token header > _csrf body field
// ============================================================
Result CSRFProtection::extractToken(
    const char* headerValue,
    const char* bodyValue,
    char*       outBuffer,
    size_t      bufferSize)
{
    if (!outBuffer)                      return Result::ERR_NULL_POINTER;
    if (bufferSize < TOKEN_BUFFER_SIZE)  return Result::ERR_BUFFER_TOO_SMALL;

    const char* source = nullptr;

    // Header takes priority (preferred for AJAX)
    if (headerValue && strlen(headerValue) == TOKEN_BUFFER_SIZE - 1) {
        source = headerValue;
    }
    // Fall back to body field (for HTML forms)
    else if (bodyValue && strlen(bodyValue) == TOKEN_BUFFER_SIZE - 1) {
        source = bodyValue;
    }

    if (!source) {
        outBuffer[0] = '\0';
        return Result::ERR_NOT_FOUND;
    }

    // Validate that token contains only hex characters
    for (size_t i = 0; i < TOKEN_BUFFER_SIZE - 1; ++i) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if (!isxdigit(c)) {
            outBuffer[0] = '\0';
            return Result::ERR_INVALID_TOKEN;
        }
    }

    strncpy(outBuffer, source, bufferSize - 1);
    outBuffer[bufferSize - 1] = '\0';

    return Result::OK;
}

// ============================================================
// constantTimeStrCompare
// Compares up to maxLen characters in constant time.
// Iterates ALL maxLen characters regardless of differences
// to prevent timing-based oracles.
// ============================================================
bool CSRFProtection::constantTimeStrCompare(
    const char* a,
    const char* b,
    size_t      maxLen) noexcept
{
    uint8_t diff = 0;

    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t ca = static_cast<uint8_t>(a[i]);
        uint8_t cb = static_cast<uint8_t>(b[i]);
        diff |= ca ^ cb;
    }

    return diff == 0;
}

} // namespace Security
} // namespace Gateway