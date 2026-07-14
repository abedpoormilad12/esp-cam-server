// ============================================================
// SecureRandom.cpp
// ============================================================

#include "SecureRandom.h"

#include <Arduino.h>
#include <esp_random.h>
#include <esp_system.h>
#include <cstring>
#include <cstdio>

// ESP32 hardware RNG register address
// Documented in ESP32 Technical Reference Manual, Chapter 24
#define ESP32_RNG_DATA_REG  (*(volatile uint32_t*)0x3FF75144)

namespace Gateway {
namespace Security {

// ============================================================
// Singleton
// ============================================================
SecureRandom& SecureRandom::getInstance() noexcept {
    static SecureRandom instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
SecureRandom::SecureRandom()
    : m_initialized(false)
    , m_stats{}
    , m_entropyAccumulator(0xDEADBEEF)
{
}

// ============================================================
// initialize
// ============================================================
Result SecureRandom::initialize() {
    if (m_initialized) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    // Warm up the hardware RNG by discarding initial samples.
    // ESP32 hardware RNG needs a few reads to stabilize.
    for (int i = 0; i < 32; ++i) {
        uint32_t discard = readHardwareRNG();
        m_entropyAccumulator ^= discard;
        m_entropyAccumulator  = (m_entropyAccumulator << 13) |
                                (m_entropyAccumulator >> 19);
        // Small delay between reads for RF noise sampling
        ets_delay_us(10);
    }

    // Mix in additional entropy sources
    m_entropyAccumulator ^= static_cast<uint32_t>(esp_timer_get_time());
    m_entropyAccumulator ^= static_cast<uint32_t>(millis());
    m_entropyAccumulator ^= static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&m_entropyAccumulator) & 0xFFFF
    );

    m_initialized = true;
    m_stats       = Stats{};
    return Result::OK;
}

// ============================================================
// isInitialized
// ============================================================
bool SecureRandom::isInitialized() const noexcept {
    return m_initialized;
}

// ============================================================
// readHardwareRNG
// Reads directly from the ESP32 hardware RNG register.
// The ESP-IDF esp_random() function also reads from this
// register but adds additional processing. We use both.
// ============================================================
uint32_t SecureRandom::readHardwareRNG() noexcept {
    // Use ESP-IDF's esp_random() as primary source —
    // it handles the hardware register correctly including
    // any required clock gating.
    return esp_random();
}

// ============================================================
// collectEntropy
// Collects multiple hardware RNG samples and mixes them.
// This provides additional diffusion beyond a single read.
// ============================================================
uint32_t SecureRandom::collectEntropy() noexcept {
    // Read 4 hardware samples
    uint32_t a = readHardwareRNG();
    uint32_t b = readHardwareRNG();
    uint32_t c = readHardwareRNG();
    uint32_t d = readHardwareRNG();

    // Combine with accumulator using different operations
    // to prevent any single weak sample from dominating
    uint32_t mixed = a;
    mixed ^= (b << 7)  | (b >> 25);
    mixed ^= (c << 13) | (c >> 19);
    mixed ^= (d << 17) | (d >> 15);

    // Update accumulator
    m_entropyAccumulator ^= mixed;
    m_entropyAccumulator ^= static_cast<uint32_t>(esp_timer_get_time());
    m_entropyAccumulator  = (m_entropyAccumulator * 1664525UL) + 1013904223UL;

    m_stats.entropyPoolRefreshCount++;

    return mixed ^ m_entropyAccumulator;
}

// ============================================================
// fillBytes
// ============================================================
Result SecureRandom::fillBytes(uint8_t* buffer, size_t length) {
    if (!m_initialized) return Result::ERR_NOT_INITIALIZED;
    if (!buffer)        return Result::ERR_NULL_POINTER;
    if (length == 0)    return Result::OK;

    size_t remaining = length;
    size_t offset    = 0;

    while (remaining > 0) {
        uint32_t word = collectEntropy();

        size_t chunk = (remaining >= sizeof(uint32_t))
                     ? sizeof(uint32_t)
                     : remaining;

        memcpy(buffer + offset, &word, chunk);
        offset    += chunk;
        remaining -= chunk;
    }

    m_stats.totalBytesGenerated += static_cast<uint32_t>(length);
    m_stats.totalCallCount++;

    return Result::OK;
}

// ============================================================
// getUInt32
// ============================================================
uint32_t SecureRandom::getUInt32() {
    m_stats.totalBytesGenerated += 4;
    m_stats.totalCallCount++;
    return collectEntropy();
}

// ============================================================
// getUInt64
// ============================================================
uint64_t SecureRandom::getUInt64() {
    uint64_t hi = collectEntropy();
    uint64_t lo = collectEntropy();
    m_stats.totalBytesGenerated += 8;
    m_stats.totalCallCount++;
    return (hi << 32) | lo;
}

// ============================================================
// getUInt32InRange
// Uses rejection sampling to avoid modulo bias.
// ============================================================
uint32_t SecureRandom::getUInt32InRange(uint32_t minVal,
                                         uint32_t maxVal) {
    if (minVal >= maxVal) return minVal;

    uint32_t range     = maxVal - minVal + 1;
    uint32_t threshold = (0xFFFFFFFFUL - range + 1) % range;

    uint32_t val;
    do {
        val = collectEntropy();
    } while (val < threshold);

    return minVal + (val % range);
}

// ============================================================
// bytesToHex — internal utility
// ============================================================
void SecureRandom::bytesToHex(const uint8_t* input,
                               size_t         inputLen,
                               char*          output,
                               size_t         outputSize) {
    static constexpr char HEX_CHARS[] = "0123456789abcdef";

    size_t maxBytes = (outputSize - 1) / 2;
    size_t count    = (inputLen < maxBytes) ? inputLen : maxBytes;

    for (size_t i = 0; i < count; ++i) {
        output[i * 2]     = HEX_CHARS[(input[i] >> 4) & 0x0F];
        output[i * 2 + 1] = HEX_CHARS[input[i] & 0x0F];
    }
    output[count * 2] = '\0';
}

// ============================================================
// generateHexToken
// ============================================================
Result SecureRandom::generateHexToken(char*  outBuffer,
                                       size_t bufferSize,
                                       size_t byteCount) {
    if (!outBuffer)                       return Result::ERR_NULL_POINTER;
    if (bufferSize < byteCount * 2 + 1)   return Result::ERR_BUFFER_TOO_SMALL;
    if (byteCount == 0 || byteCount > 64) return Result::ERR_INVALID_ARGUMENT;

    // Stack-allocated temp buffer — max 64 bytes
    uint8_t rawBytes[64];

    Result r = fillBytes(rawBytes, byteCount);
    if (GW_ERR(r)) return r;

    bytesToHex(rawBytes, byteCount, outBuffer, bufferSize);

    // Sanitize stack
    memset(rawBytes, 0, sizeof(rawBytes));

    return Result::OK;
}

// ============================================================
// generateSessionId
// ============================================================
Result SecureRandom::generateSessionId(char*  outBuffer,
                                        size_t bufferSize) {
    return generateHexToken(
        outBuffer,
        bufferSize,
        Config::Auth::SESSION_ID_BYTES
    );
}

// ============================================================
// generateCSRFToken
// ============================================================
Result SecureRandom::generateCSRFToken(char*  outBuffer,
                                        size_t bufferSize) {
    return generateHexToken(
        outBuffer,
        bufferSize,
        Config::Auth::CSRF_TOKEN_BYTES
    );
}

// ============================================================
// generateSalt
// ============================================================
Result SecureRandom::generateSalt(uint8_t* outBuffer,
                                   size_t   byteCount) {
    return fillBytes(outBuffer, byteCount);
}

// ============================================================
// getStats
// ============================================================
SecureRandom::Stats SecureRandom::getStats() const noexcept {
    return m_stats;
}

} // namespace Security
} // namespace Gateway