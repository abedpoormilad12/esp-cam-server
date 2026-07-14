// ============================================================
// BootManager.h
// Orchestrates the complete boot sequence of the gateway.
//
// Design decisions:
//   - Owns the boot sequence — no other component drives boot
//   - Each boot phase is isolated and independently failable
//   - Drives StateMachine via events — no direct state writes
//   - Boot happens in a dedicated high-priority task
//   - Provides boot progress reporting
//   - On failure: logs reason, triggers appropriate SM event
//   - Fallback to SETUP_MODE if configuration is absent
//   - Watchdog is fed during long operations
// ============================================================

#pragma once

#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H

#include "ErrorCodes.h"
#include "SystemState.h"
#include "../interfaces/IService.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>

namespace Gateway {
namespace Core {

// ============================================================
// Boot phase descriptor
// ============================================================
struct BootPhase {
    const char* name;
    uint8_t     progressPercent;
};

// ============================================================
// BootManager
// ============================================================
class BootManager final : public Interfaces::IService {
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static BootManager& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] Interfaces::ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "BootManager"; }
    [[nodiscard]] bool         isHealthy() const  override;

    // --------------------------------------------------------
    // Boot progress (0–100)
    // --------------------------------------------------------
    [[nodiscard]] uint8_t     getProgress()     const noexcept;
    [[nodiscard]] const char* getCurrentPhase() const noexcept;
    [[nodiscard]] bool        isBootComplete()  const noexcept;
    [[nodiscard]] uint32_t    getBootDurationMs() const noexcept;

private:
    BootManager();
    ~BootManager() = default;

    BootManager(const BootManager&)            = delete;
    BootManager& operator=(const BootManager&) = delete;

    // --------------------------------------------------------
    // Boot phases — each returns Result
    // --------------------------------------------------------
    Result phaseHardwareInit();
    Result phaseStorageInit();
    Result phaseConfigLoad();
    Result phaseSecurityInit();
    Result phaseNetworkInit();
    Result phaseServicesInit();
    Result phaseNetworkConnect();
    Result phaseWebServerInit();

    // --------------------------------------------------------
    // Helpers
    // --------------------------------------------------------
    void   setPhase(const char* name, uint8_t progress);
    void   feedWatchdog();
    void   onBootSuccess();
    void   onBootFailure(Result reason, SystemEvent failEvent);

    // --------------------------------------------------------
    // FreeRTOS task
    // --------------------------------------------------------
    static void taskEntry(void* param);
    void        task();

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState    m_serviceState;
    bool            m_bootComplete;
    uint8_t         m_progress;
    const char*     m_currentPhase;
    uint32_t        m_bootStartMs;
    uint32_t        m_bootEndMs;

    TaskHandle_t    m_taskHandle;
    StaticTask_t    m_taskTCB;
    static constexpr uint32_t TaskStackSize = 4096;
    uint8_t         m_taskStack[TaskStackSize];
};

} // namespace Core
} // namespace Gateway

#endif // BOOT_MANAGER_H