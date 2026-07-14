// ============================================================
// NVSStorage.h
// ESP32 Non-Volatile Storage backend.
//
// Design decisions:
//   - Wraps ESP-IDF NVS API with typed, safe interface
//   - Each instance operates in its own NVS namespace
//     (prevents key collisions between subsystems)
//   - IStorage interface compliance for backend swappability
//   - Thread-safe: NVS handle protected by mutex
//   - No heap allocation for keys/values up to MAX_VALUE_SIZE
//   - Read-Write and Read-Only open modes supported
//   - Commit is explicit: batched writes then one commit
//   - Blob support for binary data (keys, salts, hashes)
//
// NVS limitations on ESP32:
//   - Key length: max 15 characters
//   - Namespace: max 15 characters
//   - Value string: max 4000 bytes (practical limit ~500 bytes)
//   - Total NVS partition: 24KB (6 pages × 4KB)
//   - Wear leveling built-in
// ============================================================

#pragma once

#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H

#include "../interfaces/IStorage.h"
#include "../core/ErrorCodes.h"

#include <nvs_flash.h>
#include <nvs.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Storage {

class NVSStorage final : public Interfaces::IStorage {
public:
    // NVS key max length (ESP-IDF constraint)
    static constexpr size_t MAX_KEY_LENGTH   = 15;
    static constexpr size_t MAX_VALUE_LENGTH = 512;

    // --------------------------------------------------------
    // Constructor
    // namespaceName: max 15 chars, identifies the NVS partition
    // readOnly:      open in read-only mode (safer for reads)
    // --------------------------------------------------------
    explicit NVSStorage(const char* namespaceName,
                        bool        readOnly = false);
    ~NVSStorage() override;

    // --------------------------------------------------------
    // IStorage implementation
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
    // Extended API (not in IStorage base)
    // --------------------------------------------------------
    [[nodiscard]] Result readInt32(const char* key,
                                   int32_t&    outValue);

    [[nodiscard]] Result writeInt32(const char* key,
                                    int32_t     value);

    [[nodiscard]] Result readUInt64(const char* key,
                                    uint64_t&   outValue);

    [[nodiscard]] Result writeUInt64(const char* key,
                                     uint64_t    value);

    // Commit pending writes to flash
    // Must be called after a batch of writes
    [[nodiscard]] Result commit();

    // Get number of used and free entries
    [[nodiscard]] Result getStats(size_t& outUsed,
                                  size_t& outFree) const;

private:
    NVSStorage(const NVSStorage&)            = delete;
    NVSStorage& operator=(const NVSStorage&) = delete;

    // --------------------------------------------------------
    // Internal helpers
    // --------------------------------------------------------
    [[nodiscard]] Result openHandle();
    void                 closeHandle();
    [[nodiscard]] bool   validateKey(const char* key) const noexcept;
    [[nodiscard]] Result mapNVSError(esp_err_t err) const noexcept;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    char              m_namespace[16];
    bool              m_readOnly;
    bool              m_initialized;
    nvs_handle_t      m_handle;
    bool              m_handleOpen;
    SemaphoreHandle_t m_mutex;
};

} // namespace Storage
} // namespace Gateway

#endif // NVS_STORAGE_H