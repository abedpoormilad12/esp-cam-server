// ============================================================
// LittleFSStorage.h
// LittleFS filesystem backend for structured file I/O.
//
// Design decisions:
//   - Wraps Arduino LittleFS API with safe, typed interface
//   - Enforces max file size to prevent RAM exhaustion
//   - Thread-safe: mutex around all FS operations
//   - Path validation: prevents directory traversal attacks
//   - Atomic writes: write to temp file, then rename
//   - No dynamic allocation for paths (fixed-size buffers)
//   - Exposes both IStorage (key=path) and file-specific API
//
// Path security rules:
//   - Must start with '/'
//   - No ".." components (directory traversal prevention)
//   - Max path length: 64 characters
//   - Only printable ASCII characters allowed
// ============================================================

#pragma once

#ifndef LITTLEFS_STORAGE_H
#define LITTLEFS_STORAGE_H

#include "../interfaces/IStorage.h"
#include "../core/ErrorCodes.h"
#include "../core/SystemConfig.h"

#include <LittleFS.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Storage {

class LittleFSStorage final : public Interfaces::IStorage {
public:
    static constexpr size_t MAX_PATH_LENGTH    = 64;
    static constexpr size_t MAX_FILE_SIZE      = Config::Storage::MAX_FILE_SIZE_BYTES;
    static constexpr size_t READ_BUFFER_SIZE   = 256;
    static constexpr size_t WRITE_BUFFER_SIZE  = 256;

    // --------------------------------------------------------
    // Constructor
    // root: base directory prefix for all operations
    // --------------------------------------------------------
    explicit LittleFSStorage(const char* root = "/");
    ~LittleFSStorage() override;

    // --------------------------------------------------------
    // IStorage implementation
    // In LittleFS context: key = file path (relative to root)
    // --------------------------------------------------------
    [[nodiscard]] Result initialize()    override;
    [[nodiscard]] bool   isReady() const override;
    [[nodiscard]] const char* getName() const override;

    [[nodiscard]] Result readString(const char* key,
                                    char*       outBuffer,
                                    size_t      bufferSize) override;

    [[nodiscard]] Result writeString(const char* key,
                                     const char* value)    override;

    [[nodiscard]] Result readUInt32(const char* key,
                                    uint32_t&   outValue)  override;

    [[nodiscard]] Result writeUInt32(const char* key,
                                     uint32_t    value)    override;

    [[nodiscard]] Result readBytes(const char* key,
                                   uint8_t*    outBuffer,
                                   size_t      bufferSize,
                                   size_t&     outBytesRead) override;

    [[nodiscard]] Result writeBytes(const char*    key,
                                    const uint8_t* data,
                                    size_t         dataSize) override;

    [[nodiscard]] Result remove(const char* key)            override;
    [[nodiscard]] Result exists(const char* key,
                                bool&       outExists)      override;
    [[nodiscard]] Result clear()                            override;

    // --------------------------------------------------------
    // Extended File API
    // --------------------------------------------------------

    // Read entire file into caller-provided buffer
    [[nodiscard]] Result readFile(const char* path,
                                  uint8_t*    outBuffer,
                                  size_t      bufferSize,
                                  size_t&     outBytesRead);

    // Write buffer to file (atomic: temp file + rename)
    [[nodiscard]] Result writeFile(const char*    path,
                                   const uint8_t* data,
                                   size_t         dataSize);

    // Append data to existing file
    [[nodiscard]] Result appendFile(const char*    path,
                                    const uint8_t* data,
                                    size_t         dataSize);

    // Get file size in bytes
    [[nodiscard]] Result getFileSize(const char* path,
                                     size_t&     outSize);

    // Create directory (including parents)
    [[nodiscard]] Result createDirectory(const char* path);

    // List directory contents
    // callback: called for each entry (name, isDir, size)
    using DirCallback = void(*)(const char* name,
                                bool        isDir,
                                size_t      size,
                                void*       userCtx);

    [[nodiscard]] Result listDirectory(const char* path,
                                       DirCallback callback,
                                       void*       userCtx);

    // Get filesystem stats
    [[nodiscard]] Result getFSStats(size_t& outTotal,
                                    size_t& outUsed,
                                    size_t& outFree);

private:
    LittleFSStorage(const LittleFSStorage&)            = delete;
    LittleFSStorage& operator=(const LittleFSStorage&) = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    [[nodiscard]] bool   buildPath(char*       outPath,
                                   size_t      outSize,
                                   const char* relativePath) const;

    [[nodiscard]] bool   validatePath(const char* path) const noexcept;
    [[nodiscard]] Result mapFSError()                   const noexcept;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    char              m_root[MAX_PATH_LENGTH];
    bool              m_initialized;
    SemaphoreHandle_t m_mutex;
};

} // namespace Storage
} // namespace Gateway

#endif // LITTLEFS_STORAGE_H