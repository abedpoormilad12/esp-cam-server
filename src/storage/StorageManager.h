// ============================================================
// StorageManager.h
// Unified facade over all storage backends.
//
// Design decisions:
//   - Single access point for all storage operations
//   - Owns NVSStorage and LittleFSStorage instances
//   - Routes requests to the appropriate backend by prefix:
//       nvs://  → NVSStorage
//       fs://   → LittleFSStorage
//       (no prefix) → default backend (NVS for config)
//   - Implements IService for lifecycle management
//   - Provides namespace-scoped NVS access per subsystem
//   - Thread-safe by delegating to backends
// ============================================================

#pragma once

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "../interfaces/IService.h"
#include "../interfaces/IHealthCheck.h"
#include "NVSStorage.h"
#include "LittleFSStorage.h"
#include "../core/SystemConfig.h"

namespace Gateway {
namespace Storage {

class StorageManager final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static StorageManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] Interfaces::ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "StorageManager"; }
    [[nodiscard]] bool         isHealthy() const  override;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "StorageManager"; }

    // --------------------------------------------------------
    // Direct backend access (for subsystems that need it)
    // --------------------------------------------------------
    [[nodiscard]] NVSStorage&      getNVS(const char* namespaceName);
    [[nodiscard]] LittleFSStorage& getFS();

    // --------------------------------------------------------
    // Convenience: pre-configured namespace accessors
    // --------------------------------------------------------
    [[nodiscard]] NVSStorage& getConfigNVS()  { return m_nvsConfig; }
    [[nodiscard]] NVSStorage& getAuthNVS()    { return m_nvsAuth;   }
    [[nodiscard]] NVSStorage& getSystemNVS()  { return m_nvsSystem; }

    // --------------------------------------------------------
    // High-level file API (delegates to LittleFS backend)
    // --------------------------------------------------------
    [[nodiscard]] Result readJsonFile(const char* path,
                                      char*       outBuffer,
                                      size_t      bufferSize,
                                      size_t&     outSize);

    [[nodiscard]] Result writeJsonFile(const char* path,
                                       const char* jsonContent);

    [[nodiscard]] Result fileExists(const char* path,
                                    bool&       outExists);

    [[nodiscard]] Result deleteFile(const char* path);

    // --------------------------------------------------------
    // Storage health: free space check
    // --------------------------------------------------------
    [[nodiscard]] bool isStorageLow() const;
    [[nodiscard]] size_t getFreeSpaceBytes() const;

private:
    StorageManager();
    ~StorageManager() = default;

    StorageManager(const StorageManager&)            = delete;
    StorageManager& operator=(const StorageManager&) = delete;

    // --------------------------------------------------------
    // Storage backend instances — owned here
    // --------------------------------------------------------
    NVSStorage      m_nvsConfig;
    NVSStorage      m_nvsAuth;
    NVSStorage      m_nvsSystem;
    LittleFSStorage m_littleFS;

    Interfaces::ServiceState    m_state;
};

} // namespace Storage
} // namespace Gateway

#endif // STORAGE_MANAGER_H