// ============================================================
// ErrorCodes.cpp
// ============================================================

#include "ErrorCodes.h"

namespace Gateway {
namespace ResultHelper {

const char* toString(Result r) noexcept {
    switch (r) {
        case Result::OK:                              return "OK";
        case Result::ERR_UNKNOWN:                     return "Unknown error";
        case Result::ERR_NOT_INITIALIZED:             return "Not initialized";
        case Result::ERR_ALREADY_INITIALIZED:         return "Already initialized";
        case Result::ERR_INVALID_ARGUMENT:            return "Invalid argument";
        case Result::ERR_NULL_POINTER:                return "Null pointer";
        case Result::ERR_BUFFER_TOO_SMALL:            return "Buffer too small";
        case Result::ERR_TIMEOUT:                     return "Timeout";
        case Result::ERR_NOT_FOUND:                   return "Not found";
        case Result::ERR_ALREADY_EXISTS:              return "Already exists";
        case Result::ERR_NOT_SUPPORTED:               return "Not supported";
        case Result::ERR_OPERATION_FAILED:            return "Operation failed";
        case Result::ERR_RESOURCE_EXHAUSTED:          return "Resource exhausted";
        case Result::ERR_OUT_OF_MEMORY:               return "Out of memory";
        case Result::ERR_QUEUE_FULL:                  return "Queue full";
        case Result::ERR_QUEUE_EMPTY:                 return "Queue empty";
        case Result::ERR_MAX_CAPACITY:                return "Max capacity reached";
        case Result::ERR_BOOT_FAILED:                 return "Boot failed";
        case Result::ERR_INVALID_STATE:               return "Invalid state";
        case Result::ERR_STATE_TRANSITION_DENIED:     return "State transition denied";
        case Result::ERR_WATCHDOG_TRIGGERED:          return "Watchdog triggered";
        case Result::ERR_BOOT_PARTITION_INVALID:      return "Boot partition invalid";
        case Result::ERR_STORAGE_INIT_FAILED:         return "Storage init failed";
        case Result::ERR_STORAGE_READ_FAILED:         return "Storage read failed";
        case Result::ERR_STORAGE_WRITE_FAILED:        return "Storage write failed";
        case Result::ERR_STORAGE_DELETE_FAILED:       return "Storage delete failed";
        case Result::ERR_STORAGE_NOT_MOUNTED:         return "Storage not mounted";
        case Result::ERR_STORAGE_FULL:                return "Storage full";
        case Result::ERR_STORAGE_CORRUPTED:           return "Storage corrupted";
        case Result::ERR_FILE_OPEN_FAILED:            return "File open failed";
        case Result::ERR_FILE_NOT_FOUND:              return "File not found";
        case Result::ERR_FILE_TOO_LARGE:              return "File too large";
        case Result::ERR_NVS_INIT_FAILED:             return "NVS init failed";
        case Result::ERR_NVS_KEY_NOT_FOUND:           return "NVS key not found";
        case Result::ERR_NVS_WRITE_FAILED:            return "NVS write failed";
        case Result::ERR_NVS_READ_FAILED:             return "NVS read failed";
        case Result::ERR_NETWORK_INIT_FAILED:         return "Network init failed";
        case Result::ERR_WIFI_CONNECT_FAILED:         return "WiFi connect failed";
        case Result::ERR_WIFI_TIMEOUT:                return "WiFi timeout";
        case Result::ERR_WIFI_AUTH_FAILED:            return "WiFi auth failed";
        case Result::ERR_MDNS_FAILED:                 return "mDNS failed";
        case Result::ERR_SOCKET_FAILED:               return "Socket failed";
        case Result::ERR_DNS_FAILED:                  return "DNS failed";
        case Result::ERR_WEBSERVER_INIT_FAILED:       return "WebServer init failed";
        case Result::ERR_WEBSERVER_START_FAILED:      return "WebServer start failed";
        case Result::ERR_HANDLER_NOT_FOUND:           return "Handler not found";
        case Result::ERR_ROUTE_CONFLICT:              return "Route conflict";
        case Result::ERR_REQUEST_TOO_LARGE:           return "Request too large";
        case Result::ERR_RESPONSE_FAILED:             return "Response failed";
        case Result::ERR_SECURITY_INIT_FAILED:        return "Security init failed";
        case Result::ERR_CRYPTO_FAILED:               return "Crypto failed";
        case Result::ERR_RANDOM_FAILED:               return "Random generation failed";
        case Result::ERR_HASH_FAILED:                 return "Hash failed";
        case Result::ERR_INVALID_TOKEN:               return "Invalid token";
        case Result::ERR_CSRF_INVALID:                return "CSRF token invalid";
        case Result::ERR_RATE_LIMITED:                return "Rate limited";
        case Result::ERR_REPLAY_DETECTED:             return "Replay attack detected";
        case Result::ERR_AUTH_INVALID_CREDENTIALS:    return "Invalid credentials";
        case Result::ERR_AUTH_ACCOUNT_LOCKED:         return "Account locked";
        case Result::ERR_AUTH_SESSION_EXPIRED:        return "Session expired";
        case Result::ERR_AUTH_SESSION_INVALID:        return "Session invalid";
        case Result::ERR_AUTH_SESSION_FULL:           return "Session pool full";
        case Result::ERR_AUTH_UNAUTHORIZED:           return "Unauthorized";
        case Result::ERR_AUTH_FORBIDDEN:              return "Forbidden";
        case Result::ERR_AUTH_TOKEN_INVALID:          return "Auth token invalid";
        case Result::ERR_USER_NOT_FOUND:              return "User not found";
        case Result::ERR_USER_ALREADY_EXISTS:         return "User already exists";
        case Result::ERR_USER_INVALID_USERNAME:       return "Invalid username";
        case Result::ERR_USER_INVALID_PASSWORD:       return "Invalid password";
        case Result::ERR_USER_MAX_REACHED:            return "Max users reached";
        case Result::ERR_USER_CANNOT_DELETE_SELF:     return "Cannot delete self";
        case Result::ERR_USER_CANNOT_DELETE_LAST_ADMIN: return "Cannot delete last admin";
        case Result::ERR_USER_LOAD_FAILED:            return "User load failed";
        case Result::ERR_USER_SAVE_FAILED:            return "User save failed";
        case Result::ERR_CONFIG_LOAD_FAILED:          return "Config load failed";
        case Result::ERR_CONFIG_SAVE_FAILED:          return "Config save failed";
        case Result::ERR_CONFIG_INVALID:              return "Config invalid";
        case Result::ERR_CONFIG_KEY_NOT_FOUND:        return "Config key not found";
        case Result::ERR_CONFIG_TYPE_MISMATCH:        return "Config type mismatch";
        case Result::ERR_DEVICE_NOT_FOUND:            return "Device not found";
        case Result::ERR_DEVICE_ALREADY_REGISTERED:   return "Device already registered";
        case Result::ERR_DEVICE_MAX_REACHED:          return "Max devices reached";
        case Result::ERR_DEVICE_AUTH_FAILED:          return "Device auth failed";
        case Result::ERR_DEVICE_TIMEOUT:              return "Device timeout";
        case Result::ERR_DEVICE_OFFLINE:              return "Device offline";
        case Result::ERR_OTA_INIT_FAILED:             return "OTA init failed";
        case Result::ERR_OTA_DOWNLOAD_FAILED:         return "OTA download failed";
        case Result::ERR_OTA_VERIFY_FAILED:           return "OTA verify failed";
        case Result::ERR_OTA_FLASH_FAILED:            return "OTA flash failed";
        case Result::ERR_OTA_ROLLBACK_FAILED:         return "OTA rollback failed";
        default:                                      return "Unrecognized error";
    }
}

} // namespace ResultHelper
} // namespace Gateway