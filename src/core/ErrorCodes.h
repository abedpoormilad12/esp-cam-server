// ============================================================
// ErrorCodes.h
// Centralized error code definitions for the entire firmware.
// All subsystems use these codes for consistent error handling.
// ============================================================

#pragma once

#ifndef ERROR_CODES_H
#define ERROR_CODES_H

#include <cstdint>

namespace Gateway {

// ============================================================
// Result type - used throughout the codebase
// ============================================================
enum class Result : int32_t {
    // ---- Success ----
    OK                          =  0,

    // ---- Generic Errors (1xxx) ----
    ERR_UNKNOWN                 = -1000,
    ERR_NOT_INITIALIZED         = -1001,
    ERR_ALREADY_INITIALIZED     = -1002,
    ERR_INVALID_ARGUMENT        = -1003,
    ERR_NULL_POINTER            = -1004,
    ERR_BUFFER_TOO_SMALL        = -1005,
    ERR_TIMEOUT                 = -1006,
    ERR_NOT_FOUND               = -1007,
    ERR_ALREADY_EXISTS          = -1008,
    ERR_NOT_SUPPORTED           = -1009,
    ERR_OPERATION_FAILED        = -1010,
    ERR_RESOURCE_EXHAUSTED      = -1011,
    ERR_OUT_OF_MEMORY           = -1012,
    ERR_QUEUE_FULL              = -1013,
    ERR_QUEUE_EMPTY             = -1014,
    ERR_MAX_CAPACITY            = -1015,

    // ---- Boot & State Machine (2xxx) ----
    ERR_BOOT_FAILED             = -2000,
    ERR_INVALID_STATE           = -2001,
    ERR_STATE_TRANSITION_DENIED = -2002,
    ERR_WATCHDOG_TRIGGERED      = -2003,
    ERR_BOOT_PARTITION_INVALID  = -2004,

    // ---- Storage (3xxx) ----
    ERR_STORAGE_INIT_FAILED     = -3000,
    ERR_STORAGE_READ_FAILED     = -3001,
    ERR_STORAGE_WRITE_FAILED    = -3002,
    ERR_STORAGE_DELETE_FAILED   = -3003,
    ERR_STORAGE_NOT_MOUNTED     = -3004,
    ERR_STORAGE_FULL            = -3005,
    ERR_STORAGE_CORRUPTED       = -3006,
    ERR_FILE_OPEN_FAILED        = -3007,
    ERR_FILE_NOT_FOUND          = -3008,
    ERR_FILE_TOO_LARGE          = -3009,
    ERR_NVS_INIT_FAILED         = -3010,
    ERR_NVS_KEY_NOT_FOUND       = -3011,
    ERR_NVS_WRITE_FAILED        = -3012,
    ERR_NVS_READ_FAILED         = -3013,

    // ---- Network (4xxx) ----
    ERR_NETWORK_INIT_FAILED     = -4000,
    ERR_WIFI_CONNECT_FAILED     = -4001,
    ERR_WIFI_TIMEOUT            = -4002,
    ERR_WIFI_AUTH_FAILED        = -4003,
    ERR_MDNS_FAILED             = -4004,
    ERR_SOCKET_FAILED           = -4005,
    ERR_DNS_FAILED              = -4006,

    // ---- Web Server (5xxx) ----
    ERR_WEBSERVER_INIT_FAILED   = -5000,
    ERR_WEBSERVER_START_FAILED  = -5001,
    ERR_HANDLER_NOT_FOUND       = -5002,
    ERR_ROUTE_CONFLICT          = -5003,
    ERR_REQUEST_TOO_LARGE       = -5004,
    ERR_RESPONSE_FAILED         = -5005,

    // ---- Security (6xxx) ----
    ERR_SECURITY_INIT_FAILED    = -6000,
    ERR_CRYPTO_FAILED           = -6001,
    ERR_RANDOM_FAILED           = -6002,
    ERR_HASH_FAILED             = -6003,
    ERR_INVALID_TOKEN           = -6004,
    ERR_CSRF_INVALID            = -6005,
    ERR_RATE_LIMITED            = -6006,
    ERR_REPLAY_DETECTED         = -6007,

    // ---- Authentication (7xxx) ----
    ERR_AUTH_INVALID_CREDENTIALS = -7000,
    ERR_AUTH_ACCOUNT_LOCKED      = -7001,
    ERR_AUTH_SESSION_EXPIRED     = -7002,
    ERR_AUTH_SESSION_INVALID     = -7003,
    ERR_AUTH_SESSION_FULL        = -7004,
    ERR_AUTH_UNAUTHORIZED        = -7005,
    ERR_AUTH_FORBIDDEN           = -7006,
    ERR_AUTH_TOKEN_INVALID       = -7007,

    // ---- User Management (8xxx) ----
    ERR_USER_NOT_FOUND          = -8000,
    ERR_USER_ALREADY_EXISTS     = -8001,
    ERR_USER_INVALID_USERNAME   = -8002,
    ERR_USER_INVALID_PASSWORD   = -8003,
    ERR_USER_MAX_REACHED        = -8004,
    ERR_USER_CANNOT_DELETE_SELF = -8005,
    ERR_USER_CANNOT_DELETE_LAST_ADMIN = -8006,
    ERR_USER_LOAD_FAILED        = -8007,
    ERR_USER_SAVE_FAILED        = -8008,

    // ---- Config (9xxx) ----
    ERR_CONFIG_LOAD_FAILED      = -9000,
    ERR_CONFIG_SAVE_FAILED      = -9001,
    ERR_CONFIG_INVALID          = -9002,
    ERR_CONFIG_KEY_NOT_FOUND    = -9003,
    ERR_CONFIG_TYPE_MISMATCH    = -9004,

    // ---- Device Registry (10xxx) - Future ----
    ERR_DEVICE_NOT_FOUND        = -10000,
    ERR_DEVICE_ALREADY_REGISTERED = -10001,
    ERR_DEVICE_MAX_REACHED      = -10002,
    ERR_DEVICE_AUTH_FAILED      = -10003,
    ERR_DEVICE_TIMEOUT          = -10004,
    ERR_DEVICE_OFFLINE          = -10005,

    // ---- OTA (11xxx) - Future ----
    ERR_OTA_INIT_FAILED         = -11000,
    ERR_OTA_DOWNLOAD_FAILED     = -11001,
    ERR_OTA_VERIFY_FAILED       = -11002,
    ERR_OTA_FLASH_FAILED        = -11003,
    ERR_OTA_ROLLBACK_FAILED     = -11004,
};

// ============================================================
// Helper functions for Result
// ============================================================
namespace ResultHelper {

    [[nodiscard]] inline constexpr bool isOk(Result r) noexcept {
        return r == Result::OK;
    }

    [[nodiscard]] inline constexpr bool isError(Result r) noexcept {
        return r != Result::OK;
    }

    [[nodiscard]] inline constexpr int32_t toInt(Result r) noexcept {
        return static_cast<int32_t>(r);
    }

    [[nodiscard]] const char* toString(Result r) noexcept;

} // namespace ResultHelper

// Convenience macros
#define GW_OK(result)    (Gateway::ResultHelper::isOk(result))
#define GW_ERR(result)   (Gateway::ResultHelper::isError(result))
#define GW_RETURN_IF_ERR(expr)                      \
    do {                                             \
        Gateway::Result _r = (expr);                 \
        if (Gateway::ResultHelper::isError(_r)) {   \
            return _r;                               \
        }                                            \
    } while(0)

} // namespace Gateway

#endif // ERROR_CODES_H