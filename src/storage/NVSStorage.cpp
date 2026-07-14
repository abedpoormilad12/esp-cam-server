// ============================================================
// NVSStorage.cpp
// ============================================================

#include "NVSStorage.h"
#include "../services/Logger.h"

#include <nvs_flash.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Storage {

static constexpr const char* TAG = "NVSStorage";

// ============================================================
// Constructor
// ============================================================
NVSStorage::NVSStorage(const char* namespaceName, bool readOnly)
    : m_namespace{}
    , m_readOnly(readOnly)
    , m_initialized(false)
    , m_handle(0)
    , m_handleOpen(false)
    , m_mutex(nullptr)
{
    if (namespaceName) {
        strncpy(m_namespace, namespaceName, sizeof(m_namespace) - 1);
        m_namespace[sizeof(m_namespace) - 1] = '\0';
    }
}

// ============================================================
// Destructor
// ============================================================
NVSStorage::~NVSStorage() {
    closeHandle();
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

// ============================================================
// initialize
// ============================================================
Result NVSStorage::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    // Initialize NVS flash (idempotent — safe to call multiple times)
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or version mismatch
        GW_LOG_W(TAG, "NVS needs erase. Erasing and reinitializing...");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            GW_LOG_E(TAG, "NVS erase failed: %d", static_cast<int>(err));
            return Result::ERR_NVS_INIT_FAILED;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        GW_LOG_E(TAG, "NVS flash init failed: %d", static_cast<int>(err));
        return Result::ERR_NVS_INIT_FAILED;
    }

    // Open the NVS handle for this namespace
    Result r = openHandle();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "Failed to open NVS namespace '%s'", m_namespace);
        return r;
    }

    m_initialized = true;
    GW_LOG_I(TAG, "Initialized. Namespace: '%s' Mode: %s",
             m_namespace, m_readOnly ? "RO" : "RW");

    return Result::OK;
}

// ============================================================
// openHandle
// ============================================================
Result NVSStorage::openHandle() {
    nvs_open_mode_t mode = m_readOnly ? NVS_READONLY : NVS_READWRITE;

    esp_err_t err = nvs_open(m_namespace, mode, &m_handle);
    if (err != ESP_OK) {
        return mapNVSError(err);
    }

    m_handleOpen = true;
    return Result::OK;
}

// ============================================================
// closeHandle
// ============================================================
void NVSStorage::closeHandle() {
    if (m_handleOpen) {
        nvs_close(m_handle);
        m_handleOpen = false;
    }
}

// ============================================================
// isReady
// ============================================================
bool NVSStorage::isReady() const {
    return m_initialized && m_handleOpen;
}

// ============================================================
// getName
// ============================================================
const char* NVSStorage::getName() const {
    return m_namespace;
}

// ============================================================
// validateKey
// ============================================================
bool NVSStorage::validateKey(const char* key) const noexcept {
    if (!key) return false;
    size_t len = strlen(key);
    return len > 0 && len <= MAX_KEY_LENGTH;
}

// ============================================================
// mapNVSError — translate ESP-IDF errors to Gateway Result
// ============================================================
Result NVSStorage::mapNVSError(esp_err_t err) const noexcept {
    switch (err) {
        case ESP_OK:                       return Result::OK;
        case ESP_ERR_NVS_NOT_FOUND:        return Result::ERR_NVS_KEY_NOT_FOUND;
        case ESP_ERR_NVS_READ_ONLY:        return Result::ERR_STORAGE_WRITE_FAILED;
        case ESP_ERR_NVS_NOT_ENOUGH_SPACE: return Result::ERR_STORAGE_FULL;
        case ESP_ERR_NVS_INVALID_NAME:     return Result::ERR_INVALID_ARGUMENT;
        case ESP_ERR_NVS_INVALID_HANDLE:   return Result::ERR_NOT_INITIALIZED;
        case ESP_ERR_NVS_INVALID_LENGTH:   return Result::ERR_BUFFER_TOO_SMALL;
        default:                           return Result::ERR_STORAGE_READ_FAILED;
    }
}

// ============================================================
// readString
// ============================================================
Result NVSStorage::readString(const char* key,
                               char*       outBuffer,
                               size_t      bufferSize) {
    if (!outBuffer)           return Result::ERR_NULL_POINTER;
    if (!validateKey(key))    return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())           return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_get_str(m_handle, key, outBuffer, &bufferSize);

    xSemaphoreGive(m_mutex);
    return mapNVSError(err);
}

// ============================================================
// writeString
// ============================================================
Result NVSStorage::writeString(const char* key, const char* value) {
    if (!value)            return Result::ERR_NULL_POINTER;
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_set_str(m_handle, key, value);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// readUInt32
// ============================================================
Result NVSStorage::readUInt32(const char* key, uint32_t& outValue) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_get_u32(m_handle, key, &outValue);

    xSemaphoreGive(m_mutex);
    return mapNVSError(err);
}

// ============================================================
// writeUInt32
// ============================================================
Result NVSStorage::writeUInt32(const char* key, uint32_t value) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_set_u32(m_handle, key, value);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// readInt32
// ============================================================
Result NVSStorage::readInt32(const char* key, int32_t& outValue) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_get_i32(m_handle, key, &outValue);

    xSemaphoreGive(m_mutex);
    return mapNVSError(err);
}

// ============================================================
// writeInt32
// ============================================================
Result NVSStorage::writeInt32(const char* key, int32_t value) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_set_i32(m_handle, key, value);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// readUInt64
// ============================================================
Result NVSStorage::readUInt64(const char* key, uint64_t& outValue) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_get_u64(m_handle, key, &outValue);

    xSemaphoreGive(m_mutex);
    return mapNVSError(err);
}

// ============================================================
// writeUInt64
// ============================================================
Result NVSStorage::writeUInt64(const char* key, uint64_t value) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_set_u64(m_handle, key, value);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// readBytes
// ============================================================
Result NVSStorage::readBytes(const char* key,
                              uint8_t*    outBuffer,
                              size_t      bufferSize,
                              size_t&     outBytesRead) {
    if (!outBuffer)        return Result::ERR_NULL_POINTER;
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    outBytesRead  = bufferSize;
    esp_err_t err = nvs_get_blob(m_handle, key,
                                  outBuffer, &outBytesRead);

    xSemaphoreGive(m_mutex);
    return mapNVSError(err);
}

// ============================================================
// writeBytes
// ============================================================
Result NVSStorage::writeBytes(const char*    key,
                               const uint8_t* data,
                               size_t         dataSize) {
    if (!data)             return Result::ERR_NULL_POINTER;
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_set_blob(m_handle, key, data, dataSize);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// remove
// ============================================================
Result NVSStorage::remove(const char* key) {
    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (m_readOnly)        return Result::ERR_NOT_SUPPORTED;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_erase_key(m_handle, key);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    return commit();
}

// ============================================================
// exists
// ============================================================
Result NVSStorage::exists(const char* key, bool& outExists) {
    outExists = false;

    if (!validateKey(key)) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    // Attempt to get the string — if ERR_NVS_NOT_FOUND, key absent
    size_t    reqLen = 0;
    esp_err_t err    = nvs_get_str(m_handle, key, nullptr, &reqLen);

    if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
        outExists = true;
        err       = ESP_OK;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        // Also check for blob
        err = nvs_get_blob(m_handle, key, nullptr, &reqLen);
        if (err == ESP_OK || err == ESP_ERR_NVS_INVALID_LENGTH) {
            outExists = true;
            err       = ESP_OK;
        } else {
            // Try uint32
            uint32_t dummy;
            err = nvs_get_u32(m_handle, key, &dummy);
            outExists = (err == ESP_OK);
            err       = ESP_OK;
        }
    }

    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// clear — erase entire namespace
// ============================================================
Result NVSStorage::clear() {
    if (m_readOnly)  return Result::ERR_NOT_SUPPORTED;
    if (!isReady())  return Result::ERR_NOT_INITIALIZED;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    esp_err_t err = nvs_erase_all(m_handle);

    xSemaphoreGive(m_mutex);

    if (err != ESP_OK) return mapNVSError(err);

    GW_LOG_W(TAG, "Namespace '%s' cleared!", m_namespace);
    return commit();
}

// ============================================================
// commit — flush pending writes to NVS flash
// ============================================================
Result NVSStorage::commit() {
    if (!m_handleOpen) return Result::ERR_NOT_INITIALIZED;

    // commit() does NOT need the mutex (called internally
    // after mutex-protected write operations)
    esp_err_t err = nvs_commit(m_handle);
    return mapNVSError(err);
}

// ============================================================
// getStats
// ============================================================
Result NVSStorage::getStats(size_t& outUsed, size_t& outFree) const {
    nvs_stats_t stats;
    esp_err_t   err = nvs_get_stats(nullptr, &stats);

    if (err != ESP_OK) return mapNVSError(err);

    outUsed = stats.used_entries;
    outFree = stats.free_entries;
    return Result::OK;
}

} // namespace Storage
} // namespace Gateway