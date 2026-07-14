// ============================================================
// LittleFSStorage.cpp
// ============================================================

#include "LittleFSStorage.h"
#include "../services/Logger.h"

#include <cstring>
#include <cstdio>
#include <cctype>

namespace Gateway {
namespace Storage {

static constexpr const char* TAG = "LittleFSStorage";

// ============================================================
// Constructor
// ============================================================
LittleFSStorage::LittleFSStorage(const char* root)
    : m_root{}
    , m_initialized(false)
    , m_mutex(nullptr)
{
    if (root) {
        strncpy(m_root, root, sizeof(m_root) - 1);
        m_root[sizeof(m_root) - 1] = '\0';
    } else {
        m_root[0] = '/';
        m_root[1] = '\0';
    }
}

// ============================================================
// Destructor
// ============================================================
LittleFSStorage::~LittleFSStorage() {
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

// ============================================================
// initialize
// ============================================================
Result LittleFSStorage::initialize() {
    if (m_initialized) return Result::ERR_ALREADY_INITIALIZED;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    // LittleFS.begin() already called by BootManager
    // Verify it is mounted
    if (!LittleFS.begin(false)) {
        GW_LOG_E(TAG, "LittleFS not mounted");
        return Result::ERR_STORAGE_NOT_MOUNTED;
    }

    // Ensure root directory exists
    if (strlen(m_root) > 1) {
        if (!LittleFS.exists(m_root)) {
            if (!LittleFS.mkdir(m_root)) {
                GW_LOG_E(TAG, "Failed to create root dir: %s", m_root);
                return Result::ERR_STORAGE_INIT_FAILED;
            }
        }
    }

    m_initialized = true;

    size_t total, used, free;
    if (GW_OK(getFSStats(total, used, free))) {
        GW_LOG_I(TAG, "Ready. Root:'%s' Total:%zuKB Used:%zuKB Free:%zuKB",
                 m_root,
                 total / 1024,
                 used  / 1024,
                 free  / 1024);
    }

    return Result::OK;
}

// ============================================================
// isReady
// ============================================================
bool LittleFSStorage::isReady() const {
    return m_initialized;
}

// ============================================================
// getName
// ============================================================
const char* LittleFSStorage::getName() const {
    return "LittleFS";
}

// ============================================================
// validatePath — security check
// ============================================================
bool LittleFSStorage::validatePath(const char* path) const noexcept {
    if (!path || path[0] == '\0') return false;

    size_t len = strlen(path);
    if (len >= MAX_PATH_LENGTH) return false;

    // No directory traversal
    if (strstr(path, "..")) return false;

    // Only printable ASCII, no control characters
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(path[i]);
        if (!isprint(c) && c != '/') return false;
    }

    return true;
}

// ============================================================
// buildPath — prepend root to relative path
// ============================================================
bool LittleFSStorage::buildPath(char*       outPath,
                                 size_t      outSize,
                                 const char* relativePath) const {
    if (!outPath || !relativePath) return false;
    if (!validatePath(relativePath)) return false;

    // If path already starts with '/', treat as absolute
    if (relativePath[0] == '/') {
        int n = snprintf(outPath, outSize, "%s", relativePath);
        return (n > 0 && static_cast<size_t>(n) < outSize);
    }

    int n = snprintf(outPath, outSize, "%s/%s", m_root, relativePath);
    return (n > 0 && static_cast<size_t>(n) < outSize);
}

// ============================================================
// mapFSError
// ============================================================
Result LittleFSStorage::mapFSError() const noexcept {
    return Result::ERR_STORAGE_READ_FAILED;
}

// ============================================================
// readFile
// ============================================================
Result LittleFSStorage::readFile(const char* path,
                                  uint8_t*    outBuffer,
                                  size_t      bufferSize,
                                  size_t&     outBytesRead) {
    if (!outBuffer)        return Result::ERR_NULL_POINTER;
    if (!isReady())        return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result result = Result::OK;
    outBytesRead  = 0;

    File file = LittleFS.open(fullPath, "r");
    if (!file) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_NOT_FOUND;
    }

    size_t fileSize = file.size();

    if (fileSize > MAX_FILE_SIZE) {
        file.close();
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_TOO_LARGE;
    }

    if (fileSize > bufferSize) {
        file.close();
        xSemaphoreGive(m_mutex);
        return Result::ERR_BUFFER_TOO_SMALL;
    }

    size_t bytesRead = file.read(outBuffer, fileSize);
    outBytesRead     = bytesRead;

    if (bytesRead != fileSize) {
        result = Result::ERR_STORAGE_READ_FAILED;
    }

    file.close();
    xSemaphoreGive(m_mutex);
    return result;
}

// ============================================================
// writeFile — atomic write via temp file
// ============================================================
Result LittleFSStorage::writeFile(const char*    path,
                                   const uint8_t* data,
                                   size_t         dataSize) {
    if (!data)          return Result::ERR_NULL_POINTER;
    if (!isReady())     return Result::ERR_NOT_INITIALIZED;
    if (dataSize == 0)  return Result::ERR_INVALID_ARGUMENT;
    if (dataSize > MAX_FILE_SIZE) return Result::ERR_FILE_TOO_LARGE;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    // Build temp path: append ".tmp"
    char tempPath[MAX_PATH_LENGTH + 4];
    if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", fullPath) >=
        static_cast<int>(sizeof(tempPath))) {
        return Result::ERR_BUFFER_TOO_SMALL;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result result = Result::OK;

    // Write to temp file first
    File file = LittleFS.open(tempPath, "w");
    if (!file) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_OPEN_FAILED;
    }

    size_t written = file.write(data, dataSize);
    file.close();

    if (written != dataSize) {
        LittleFS.remove(tempPath);
        xSemaphoreGive(m_mutex);
        return Result::ERR_STORAGE_WRITE_FAILED;
    }

    // Atomic rename: remove old, rename temp to final
    LittleFS.remove(fullPath);
    if (!LittleFS.rename(tempPath, fullPath)) {
        LittleFS.remove(tempPath);
        result = Result::ERR_STORAGE_WRITE_FAILED;
    }

    xSemaphoreGive(m_mutex);
    return result;
}

// ============================================================
// appendFile
// ============================================================
Result LittleFSStorage::appendFile(const char*    path,
                                    const uint8_t* data,
                                    size_t         dataSize) {
    if (!data || dataSize == 0) return Result::ERR_INVALID_ARGUMENT;
    if (!isReady())             return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    File file = LittleFS.open(fullPath, "a");
    if (!file) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_OPEN_FAILED;
    }

    // Check total size
    size_t currentSize = file.size();
    if (currentSize + dataSize > MAX_FILE_SIZE) {
        file.close();
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_TOO_LARGE;
    }

    size_t written = file.write(data, dataSize);
    file.close();

    xSemaphoreGive(m_mutex);

    return (written == dataSize)
        ? Result::OK
        : Result::ERR_STORAGE_WRITE_FAILED;
}

// ============================================================
// getFileSize
// ============================================================
Result LittleFSStorage::getFileSize(const char* path,
                                     size_t&     outSize) {
    outSize = 0;
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    File file = LittleFS.open(fullPath, "r");
    if (!file) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_NOT_FOUND;
    }

    outSize = file.size();
    file.close();
    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// createDirectory
// ============================================================
Result LittleFSStorage::createDirectory(const char* path) {
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    bool ok = LittleFS.mkdir(fullPath);

    xSemaphoreGive(m_mutex);
    return ok ? Result::OK : Result::ERR_STORAGE_WRITE_FAILED;
}

// ============================================================
// listDirectory
// ============================================================
Result LittleFSStorage::listDirectory(const char* path,
                                       DirCallback callback,
                                       void*       userCtx) {
    if (!callback) return Result::ERR_NULL_POINTER;
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), path)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    File dir = LittleFS.open(fullPath);
    if (!dir || !dir.isDirectory()) {
        xSemaphoreGive(m_mutex);
        return Result::ERR_FILE_NOT_FOUND;
    }

    File entry = dir.openNextFile();
    while (entry) {
        callback(
            entry.name(),
            entry.isDirectory(),
            entry.isDirectory() ? 0 : entry.size(),
            userCtx
        );
        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();
    xSemaphoreGive(m_mutex);
    return Result::OK;
}

// ============================================================
// getFSStats
// ============================================================
Result LittleFSStorage::getFSStats(size_t& outTotal,
                                    size_t& outUsed,
                                    size_t& outFree) {
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    outTotal = LittleFS.totalBytes();
    outUsed  = LittleFS.usedBytes();
    outFree  = outTotal - outUsed;
    return Result::OK;
}

// ============================================================
// IStorage interface — delegates to file-based API
// ============================================================
Result LittleFSStorage::readString(const char* key,
                                    char*       outBuffer,
                                    size_t      bufferSize) {
    if (!outBuffer) return Result::ERR_NULL_POINTER;

    size_t  bytesRead = 0;
    Result  r         = readFile(key,
                                  reinterpret_cast<uint8_t*>(outBuffer),
                                  bufferSize - 1,
                                  bytesRead);
    if (GW_OK(r)) {
        outBuffer[bytesRead] = '\0';
    }
    return r;
}

Result LittleFSStorage::writeString(const char* key,
                                     const char* value) {
    if (!value) return Result::ERR_NULL_POINTER;
    return writeFile(key,
                     reinterpret_cast<const uint8_t*>(value),
                     strlen(value));
}

Result LittleFSStorage::readUInt32(const char* key,
                                    uint32_t&   outValue) {
    char    buf[12];
    size_t  bytesRead = 0;
    Result  r         = readFile(key,
                                  reinterpret_cast<uint8_t*>(buf),
                                  sizeof(buf) - 1,
                                  bytesRead);
    if (GW_ERR(r)) return r;
    buf[bytesRead] = '\0';
    outValue = static_cast<uint32_t>(strtoul(buf, nullptr, 10));
    return Result::OK;
}

Result LittleFSStorage::writeUInt32(const char* key,
                                     uint32_t    value) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(value));
    return writeString(key, buf);
}

Result LittleFSStorage::readBytes(const char* key,
                                   uint8_t*    outBuffer,
                                   size_t      bufferSize,
                                   size_t&     outBytesRead) {
    return readFile(key, outBuffer, bufferSize, outBytesRead);
}

Result LittleFSStorage::writeBytes(const char*    key,
                                    const uint8_t* data,
                                    size_t         dataSize) {
    return writeFile(key, data, dataSize);
}

Result LittleFSStorage::remove(const char* key) {
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), key)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    bool ok = LittleFS.remove(fullPath);
    xSemaphoreGive(m_mutex);

    return ok ? Result::OK : Result::ERR_STORAGE_DELETE_FAILED;
}

Result LittleFSStorage::exists(const char* key, bool& outExists) {
    outExists = false;
    if (!isReady()) return Result::ERR_NOT_INITIALIZED;

    char fullPath[MAX_PATH_LENGTH];
    if (!buildPath(fullPath, sizeof(fullPath), key)) {
        return Result::ERR_INVALID_ARGUMENT;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    outExists = LittleFS.exists(fullPath);
    xSemaphoreGive(m_mutex);
    return Result::OK;
}

Result LittleFSStorage::clear() {
    GW_LOG_W(TAG, "LittleFS clear() called — removing all files in %s",
             m_root);
    // LittleFS does not provide a recursive delete API easily.
    // Format the filesystem.
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    LittleFS.end();
    bool ok = LittleFS.format();
    LittleFS.begin(false);

    xSemaphoreGive(m_mutex);
    return ok ? Result::OK : Result::ERR_STORAGE_WRITE_FAILED;
}

} // namespace Storage
} // namespace Gateway