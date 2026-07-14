// ============================================================
// SecureRandom.h
// Cryptographically secure random number generation.
//
// Design decisions:
//   - Uses ESP32 hardware RNG (register-based true RNG)
//   - ESP32 RNG is seeded from RF noise + thermal noise
//   - Wraps esp_random() which is the correct ESP-IDF API
//   - Additional entropy mixing via XOR folding
//   - Thread-safe: atomic operations + mutex for buffer ops
//   - No heap allocation: all buffers are caller-provided
//   - Provides hex-string output for tokens/session IDs
//
// Security note:
//   esp_random() on ESP32 uses a hardware RNG that is
//   continuously re-seeded from analog noise sources.
//   It is suitable for cryptographic use when WiFi/BT
//   is active (additional RF entropy). We enforce this
//   by checking WiFi state before critical operations.
// ============================================================

#pragma once

#ifndef SECURE_RANDOM_H
#define SECURE_RANDOM_H

#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Security {

class SecureRandom final {
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static SecureRandom& getInstance() noexcept;

    // --------------------------------------------------------
    // Initialization
    // Must be called after WiFi driver is initialized to
    // ensure maximum entropy from RF noise sources.
    // --------------------------------------------------------
    [[nodiscard]] Result initialize();
    [[nodiscard]] bool   isInitialized() const noexcept;

    // --------------------------------------------------------
    // Core API
    // --------------------------------------------------------

    // Fill buffer with cryptographically secure random bytes
    [[nodiscard]] Result fillBytes(uint8_t* buffer,
                                   size_t   length);

    // Generate a random uint32_t
    [[nodiscard]] uint32_t getUInt32();

    // Generate a random uint64_t
    [[nodiscard]] uint64_t getUInt64();

    // Generate random bytes in [min, max] range
    [[nodiscard]] uint32_t getUInt32InRange(uint32_t minVal,
                                             uint32_t maxVal);

    // --------------------------------------------------------
    // Token generation API
    // Generates cryptographically secure tokens as hex strings.
    // outBuffer must be at least (byteCount * 2 + 1) bytes.
    // --------------------------------------------------------
    [[nodiscard]] Result generateHexToken(char*   outBuffer,
                                          size_t  bufferSize,
                                          size_t  byteCount);

    // Generate a session ID (64 hex chars = 32 bytes entropy)
    [[nodiscard]] Result generateSessionId(
        char*  outBuffer,
        size_t bufferSize
    );

    // Generate a CSRF token (64 hex chars = 32 bytes entropy)
    [[nodiscard]] Result generateCSRFToken(
        char*  outBuffer,
        size_t bufferSize
    );

    // Generate a salt for PBKDF2 (binary output)
    [[nodiscard]] Result generateSalt(
        uint8_t* outBuffer,
        size_t   byteCount
    );

    // --------------------------------------------------------
    // Diagnostics
    // --------------------------------------------------------
    struct Stats {
        uint32_t totalBytesGenerated;
        uint32_t totalCallCount;
        uint32_t entropyPoolRefreshCount;
    };

    [[nodiscard]] Stats getStats() const noexcept;

private:
    SecureRandom();
    ~SecureRandom() = default;

    SecureRandom(const SecureRandom&)            = delete;
    SecureRandom& operator=(const SecureRandom&) = delete;

    // --------------------------------------------------------
    // Internal entropy functions
    // --------------------------------------------------------

    // Read from ESP32 hardware RNG register directly
    [[nodiscard]] uint32_t readHardwareRNG() noexcept;

    // Mix multiple hardware samples for additional diffusion
    [[nodiscard]] uint32_t collectEntropy() noexcept;

    // Convert binary buffer to lowercase hex string
    static void bytesToHex(const uint8_t* input,
                            size_t         inputLen,
                            char*          output,
                            size_t         outputSize);

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    bool     m_initialized;
    Stats    m_stats;

    // Simple entropy accumulator — XORed into each generation
    uint32_t m_entropyAccumulator;
};

} // namespace Security
} // namespace Gateway

#endif // SECURE_RANDOM_H