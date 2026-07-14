// ============================================================
// PBKDF2.h
// Password-Based Key Derivation Function 2 (PBKDF2-HMAC-SHA256)
//
// Design decisions:
//   - Built on mbedTLS (already present in ESP32 Arduino SDK)
//   - PBKDF2-HMAC-SHA256: industry standard, no licensing issues
//   - Iteration count: 1000 (optimized for ESP32 ~80ms latency)
//   - Salt: 16 bytes (128-bit) random per password
//   - Output: 32 bytes (256-bit) derived key
//   - Storage format: "pbkdf2$iterations$hexsalt$hexhash"
//     This is a standard password hash interchange format.
//   - Constant-time comparison to prevent timing attacks
//   - No dynamic memory allocation: all buffers are stack-local
//
// Why NOT bcrypt on ESP32:
//   bcrypt with cost=10 takes ~3-5 seconds on ESP32 which
//   causes WDT resets and blocks the web server. PBKDF2 with
//   1000 iterations takes ~80ms — acceptable for login UX
//   while still being computationally expensive enough to
//   resist brute-force attacks on this hardware class.
//
// Storage format example:
//   "pbkdf2$1000$a1b2c3d4...16bytes_hex...$e5f6a7b8...32bytes_hex..."
//   Total stored length: ~140 characters
// ============================================================

#pragma once

#ifndef PBKDF2_H
#define PBKDF2_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Security {

class PBKDF2 final {
public:
    // --------------------------------------------------------
    // Constants
    // --------------------------------------------------------
    static constexpr uint32_t ITERATIONS   = Config::Auth::PBKDF2_ITERATIONS;
    static constexpr size_t   SALT_BYTES   = Config::Auth::PBKDF2_SALT_BYTES;
    static constexpr size_t   KEY_BYTES    = Config::Auth::PBKDF2_KEY_BYTES;

    // Stored hash string format:
    // "pbkdf2$ITER$HEX_SALT$HEX_KEY"
    // Salt hex: SALT_BYTES * 2 = 32 chars
    // Key hex:  KEY_BYTES  * 2 = 64 chars
    // Prefix + delimiters: ~15 chars
    static constexpr size_t   HASH_STRING_SIZE = 140;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static PBKDF2& getInstance() noexcept;

    // --------------------------------------------------------
    // Hash a plaintext password.
    // Generates a random salt internally.
    // outHashString must be at least HASH_STRING_SIZE bytes.
    // --------------------------------------------------------
    [[nodiscard]] Result hashPassword(
        const char* password,
        char*       outHashString,
        size_t      hashStringSize
    );

    // --------------------------------------------------------
    // Verify a plaintext password against a stored hash string.
    // Uses constant-time comparison.
    // --------------------------------------------------------
    [[nodiscard]] Result verifyPassword(
        const char* password,
        const char* storedHashString,
        bool&       outMatch
    );

    // --------------------------------------------------------
    // Low-level PBKDF2-HMAC-SHA256 derivation.
    // Exposed for testing and flexibility.
    // --------------------------------------------------------
    [[nodiscard]] Result deriveKey(
        const uint8_t* password,
        size_t         passwordLen,
        const uint8_t* salt,
        size_t         saltLen,
        uint32_t       iterations,
        uint8_t*       outKey,
        size_t         keyLen
    );

    // --------------------------------------------------------
    // Validate password meets policy requirements
    // --------------------------------------------------------
    [[nodiscard]] Result validatePasswordPolicy(
        const char* password
    ) const;

private:
    PBKDF2();
    ~PBKDF2() = default;

    PBKDF2(const PBKDF2&)            = delete;
    PBKDF2& operator=(const PBKDF2&) = delete;

    // --------------------------------------------------------
    // Constant-time memory comparison (prevents timing attacks)
    // --------------------------------------------------------
    [[nodiscard]] static bool constantTimeCompare(
        const uint8_t* a,
        const uint8_t* b,
        size_t         length
    ) noexcept;

    // --------------------------------------------------------
    // Hex encode/decode helpers
    // --------------------------------------------------------
    static void   bytesToHex(const uint8_t* input,
                              size_t         len,
                              char*          output,
                              size_t         outputSize);

    [[nodiscard]] static Result hexToBytes(const char* hex,
                                           size_t      hexLen,
                                           uint8_t*    output,
                                           size_t      outputSize);
};

} // namespace Security
} // namespace Gateway

#endif // PBKDF2_H