// ============================================================
// PBKDF2.cpp
// ============================================================

#include "PBKDF2.h"
#include "SecureRandom.h"

#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>
#include <mbedtls/md_internal.h>

#include <cstring>
#include <cstdio>
#include <cctype>

namespace Gateway {
namespace Security {

// ============================================================
// Singleton
// ============================================================
PBKDF2& PBKDF2::getInstance() noexcept {
    static PBKDF2 instance;
    return instance;
}

PBKDF2::PBKDF2() = default;

// ============================================================
// deriveKey — core PBKDF2-HMAC-SHA256 via mbedTLS
// ============================================================
Result PBKDF2::deriveKey(
    const uint8_t* password,
    size_t         passwordLen,
    const uint8_t* salt,
    size_t         saltLen,
    uint32_t       iterations,
    uint8_t*       outKey,
    size_t         keyLen)
{
    if (!password || !salt || !outKey) return Result::ERR_NULL_POINTER;
    if (passwordLen == 0)              return Result::ERR_INVALID_ARGUMENT;
    if (saltLen == 0)                  return Result::ERR_INVALID_ARGUMENT;
    if (keyLen == 0)                   return Result::ERR_INVALID_ARGUMENT;

    // mbedTLS PBKDF2 context
    const mbedtls_md_info_t* mdInfo =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (!mdInfo) {
        return Result::ERR_CRYPTO_FAILED;
    }

    // Some mbedTLS builds expect an mbedtls_md_context_t* instead of
    // const mbedtls_md_info_t*. Create and setup a md context and use that.
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);

    int ret = mbedtls_md_setup(&mdCtx, mdInfo, 1);
    if (ret != 0) {
        mbedtls_md_free(&mdCtx);
        return Result::ERR_CRYPTO_FAILED;
    }

    // Use mbedtls_pkcs5_pbkdf2_hmac which may accept the md context in this build
    ret = mbedtls_pkcs5_pbkdf2_hmac(
        &mdCtx,
        password,
        passwordLen,
        salt,
        saltLen,
        iterations,
        static_cast<uint32_t>(keyLen),
        outKey
    );

    mbedtls_md_free(&mdCtx);

    if (ret != 0) {
        return Result::ERR_HASH_FAILED;
    }

    return Result::OK;
}

// ============================================================
// hashPassword
// ============================================================
Result PBKDF2::hashPassword(
    const char* password,
    char*       outHashString,
    size_t      hashStringSize)
{
    if (!password || !outHashString)  return Result::ERR_NULL_POINTER;
    if (hashStringSize < HASH_STRING_SIZE) return Result::ERR_BUFFER_TOO_SMALL;

    size_t pwdLen = strlen(password);
    if (pwdLen == 0) return Result::ERR_INVALID_ARGUMENT;

    // Validate policy first
    Result r = validatePasswordPolicy(password);
    if (GW_ERR(r)) return r;

    // Generate random salt
    uint8_t salt[SALT_BYTES];
    r = SecureRandom::getInstance().generateSalt(salt, SALT_BYTES);
    if (GW_ERR(r)) return r;

    // Derive key
    uint8_t key[KEY_BYTES];
    r = deriveKey(
        reinterpret_cast<const uint8_t*>(password),
        pwdLen,
        salt,
        SALT_BYTES,
        ITERATIONS,
        key,
        KEY_BYTES
    );

    if (GW_ERR(r)) {
        memset(key, 0, sizeof(key));
        return r;
    }

    // Encode salt and key as hex
    char saltHex[SALT_BYTES * 2 + 1];
    char keyHex[KEY_BYTES  * 2 + 1];

    bytesToHex(salt, SALT_BYTES, saltHex, sizeof(saltHex));
    bytesToHex(key,  KEY_BYTES,  keyHex,  sizeof(keyHex));

    // Format: "pbkdf2$ITER$SALT_HEX$KEY_HEX"
    int written = snprintf(
        outHashString,
        hashStringSize,
        "pbkdf2$%lu$%s$%s",
        static_cast<unsigned long>(ITERATIONS),
        saltHex,
        keyHex
    );

    // Sanitize sensitive data from stack
    memset(salt,    0, sizeof(salt));
    memset(key,     0, sizeof(key));
    memset(saltHex, 0, sizeof(saltHex));
    memset(keyHex,  0, sizeof(keyHex));

    if (written < 0 || static_cast<size_t>(written) >= hashStringSize) {
        memset(outHashString, 0, hashStringSize);
        return Result::ERR_BUFFER_TOO_SMALL;
    }

    return Result::OK;
}

// ============================================================
// verifyPassword
// ============================================================
Result PBKDF2::verifyPassword(
    const char* password,
    const char* storedHashString,
    bool&       outMatch)
{
    outMatch = false;

    if (!password || !storedHashString) return Result::ERR_NULL_POINTER;

    size_t pwdLen = strlen(password);
    if (pwdLen == 0) return Result::ERR_INVALID_ARGUMENT;

    // Parse stored hash string: "pbkdf2$ITER$SALT_HEX$KEY_HEX"
    char format[8];
    unsigned long storedIter = 0;
    char saltHex[SALT_BYTES * 2 + 2];
    char keyHex[KEY_BYTES   * 2 + 2];

    // Manual parsing to avoid sscanf security concerns
    const char* ptr = storedHashString;

    // Parse "pbkdf2"
    if (strncmp(ptr, "pbkdf2$", 7) != 0) {
        return Result::ERR_INVALID_TOKEN;
    }
    ptr += 7;

    // Parse iterations
    char* endPtr = nullptr;
    storedIter = strtoul(ptr, &endPtr, 10);
    if (!endPtr || *endPtr != '$' || storedIter == 0) {
        return Result::ERR_INVALID_TOKEN;
    }
    ptr = endPtr + 1;

    // Parse salt hex (exactly SALT_BYTES * 2 hex chars)
    size_t saltHexLen = SALT_BYTES * 2;
    if (strlen(ptr) < saltHexLen + 1 || ptr[saltHexLen] != '$') {
        return Result::ERR_INVALID_TOKEN;
    }
    memcpy(saltHex, ptr, saltHexLen);
    saltHex[saltHexLen] = '\0';
    ptr += saltHexLen + 1;

    // Parse key hex (exactly KEY_BYTES * 2 hex chars)
    size_t keyHexLen = KEY_BYTES * 2;
    if (strlen(ptr) < keyHexLen) {
        return Result::ERR_INVALID_TOKEN;
    }
    memcpy(keyHex, ptr, keyHexLen);
    keyHex[keyHexLen] = '\0';

    // Decode salt and stored key from hex
    uint8_t salt[SALT_BYTES];
    uint8_t storedKey[KEY_BYTES];

    Result r = hexToBytes(saltHex, saltHexLen, salt, SALT_BYTES);
    if (GW_ERR(r)) return r;

    r = hexToBytes(keyHex, keyHexLen, storedKey, KEY_BYTES);
    if (GW_ERR(r)) return r;

    // Derive key from provided password using stored salt + iterations
    uint8_t derivedKey[KEY_BYTES];
    r = deriveKey(
        reinterpret_cast<const uint8_t*>(password),
        pwdLen,
        salt,
        SALT_BYTES,
        static_cast<uint32_t>(storedIter),
        derivedKey,
        KEY_BYTES
    );

    if (GW_ERR(r)) {
        memset(derivedKey, 0, sizeof(derivedKey));
        memset(salt,       0, sizeof(salt));
        memset(storedKey,  0, sizeof(storedKey));
        return r;
    }

    // Constant-time comparison
    outMatch = constantTimeCompare(derivedKey, storedKey, KEY_BYTES);

    // Sanitize all sensitive stack data
    memset(derivedKey, 0, sizeof(derivedKey));
    memset(salt,       0, sizeof(salt));
    memset(storedKey,  0, sizeof(storedKey));
    memset(saltHex,    0, sizeof(saltHex));
    memset(keyHex,     0, sizeof(keyHex));

    return Result::OK;
}

// ============================================================
// validatePasswordPolicy
// ============================================================
Result PBKDF2::validatePasswordPolicy(const char* password) const {
    if (!password) return Result::ERR_NULL_POINTER;

    size_t len = strlen(password);

    if (len < Config::UserConfig::PASSWORD_MIN_LEN) {
        return Result::ERR_USER_INVALID_PASSWORD;
    }

    if (len > Config::UserConfig::PASSWORD_MAX_LEN) {
        return Result::ERR_USER_INVALID_PASSWORD;
    }

    // Require at least one uppercase, one lowercase, one digit
    bool hasUpper  = false;
    bool hasLower  = false;
    bool hasDigit  = false;

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(password[i]);
        if (isupper(c)) hasUpper = true;
        if (islower(c)) hasLower = true;
        if (isdigit(c)) hasDigit = true;
    }

    if (!hasUpper || !hasLower || !hasDigit) {
        return Result::ERR_USER_INVALID_PASSWORD;
    }

    return Result::OK;
}

// ============================================================
// constantTimeCompare
// Timing-safe comparison — prevents oracle attacks.
// Always compares all bytes regardless of early differences.
// ============================================================
bool PBKDF2::constantTimeCompare(
    const uint8_t* a,
    const uint8_t* b,
    size_t         length) noexcept
{
    uint8_t diff = 0;
    for (size_t i = 0; i < length; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

// ============================================================
// bytesToHex
// ============================================================
void PBKDF2::bytesToHex(
    const uint8_t* input,
    size_t         len,
    char*          output,
    size_t         outputSize)
{
    static constexpr char HEX_TABLE[] = "0123456789abcdef";
    size_t maxBytes = (outputSize - 1) / 2;
    size_t count    = (len < maxBytes) ? len : maxBytes;

    for (size_t i = 0; i < count; ++i) {
        output[i * 2]     = HEX_TABLE[(input[i] >> 4) & 0x0F];
        output[i * 2 + 1] = HEX_TABLE[input[i] & 0x0F];
    }
    output[count * 2] = '\0';
}

// ============================================================
// hexToBytes
// ============================================================
Result PBKDF2::hexToBytes(
    const char* hex,
    size_t      hexLen,
    uint8_t*    output,
    size_t      outputSize)
{
    if (!hex || !output)     return Result::ERR_NULL_POINTER;
    if (hexLen % 2 != 0)     return Result::ERR_INVALID_ARGUMENT;
    if (hexLen / 2 > outputSize) return Result::ERR_BUFFER_TOO_SMALL;

    for (size_t i = 0; i < hexLen; i += 2) {
        uint8_t hi = 0;
        uint8_t lo = 0;

        char ch = hex[i];
        if      (ch >= '0' && ch <= '9') hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        else return Result::ERR_INVALID_ARGUMENT;

        ch = hex[i + 1];
        if      (ch >= '0' && ch <= '9') lo = ch - '0';
        else if (ch >= 'a' && ch <= 'f') lo = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') lo = ch - 'A' + 10;
        else return Result::ERR_INVALID_ARGUMENT;

        output[i / 2] = (hi << 4) | lo;
    }

    return Result::OK;
}

} // namespace Security
} // namespace Gateway