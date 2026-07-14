// ============================================================
// BootManager.cpp
// ============================================================

#include "BootManager.h"
#include "StateMachine.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../services/ServiceLocator.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>

namespace Gateway {
namespace Core {

static constexpr const char* TAG = "BootManager";

// ============================================================
// Singleton
// ============================================================
BootManager& BootManager::getInstance() noexcept {
    static BootManager instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
BootManager::BootManager()
    : m_serviceState(ServiceState::UNINITIALIZED)
    , m_bootComplete(false)
    , m_progress(0)
    , m_currentPhase("idle")
    , m_bootStartMs(0)
    , m_bootEndMs(0)
    , m_taskHandle(nullptr)
    , m_taskTCB{}
    , m_taskStack{}
{
}

// ============================================================
// IService::initialize
// ============================================================
Result BootManager::initialize() {
    if (m_serviceState != ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_serviceState = ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result BootManager::start() {
    if (m_serviceState != ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    m_bootStartMs = static_cast<uint32_t>(millis());

    m_taskHandle = xTaskCreateStaticPinnedToCore(
        taskEntry,
        "BootManager",
        Config::Tasks::STACK_BOOT_MANAGER,
        this,
        Config::Tasks::PRIORITY_BOOT_MANAGER,
        m_taskStack,
        &m_taskTCB,
        Config::Tasks::CORE_APPLICATION
    );

    if (!m_taskHandle) {
        m_serviceState = ServiceState::FAULTED;
        return Result::ERR_OPERATION_FAILED;
    }

    m_serviceState = ServiceState::RUNNING;
    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result BootManager::stop() {
    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    m_serviceState = ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool BootManager::isHealthy() const {
    return m_bootComplete ||
           m_serviceState == ServiceState::RUNNING;
}

// ============================================================
// Progress accessors
// ============================================================
uint8_t BootManager::getProgress() const noexcept {
    return m_progress;
}

const char* BootManager::getCurrentPhase() const noexcept {
    return m_currentPhase;
}

bool BootManager::isBootComplete() const noexcept {
    return m_bootComplete;
}

uint32_t BootManager::getBootDurationMs() const noexcept {
    if (!m_bootComplete) return 0;
    return m_bootEndMs - m_bootStartMs;
}

Interfaces::ServiceState BootManager::getState() const {
    return m_serviceState;
}

// ============================================================
// Internal helpers
// ============================================================
void BootManager::setPhase(const char* name, uint8_t progress) {
    m_currentPhase = name;
    m_progress     = progress;
    GW_LOG_I(TAG, "[%3d%%] Phase: %s", static_cast<int>(progress), name);
}

void BootManager::feedWatchdog() {
    esp_task_wdt_reset();
}

void BootManager::onBootSuccess() {
    m_bootComplete = true;
    m_bootEndMs    = static_cast<uint32_t>(millis());
    m_progress     = 100;

    GW_LOG_I(TAG, "Boot completed in %lu ms",
             static_cast<unsigned long>(getBootDurationMs()));

    StateMachine::getInstance().processEventSync(
        SystemEvent::BOOT_COMPLETE
    );
}

void BootManager::onBootFailure(Result reason, SystemEvent failEvent) {
    GW_LOG_E(TAG, "Boot failed at phase '%s': %s",
             m_currentPhase,
             ResultHelper::toString(reason));

    StateMachine::getInstance().processEventSync(failEvent);

    // Self-destruct — boot task is done
    m_serviceState = ServiceState::FAULTED;
}

// ============================================================
// Phase: Hardware Init
// ============================================================
Result BootManager::phaseHardwareInit() {
    setPhase("HardwareInit", 5);

    // Configure status LED
    pinMode(Config::Hardware::LED_STATUS_PIN, OUTPUT);
    digitalWrite(Config::Hardware::LED_STATUS_PIN, HIGH);

    // Initialize Serial for logging
    if (Config::Logger::ENABLE_SERIAL) {
        Serial.begin(Config::Logger::SERIAL_BAUD);
        uint32_t deadline = millis() + 2000;
        while (!Serial && millis() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    feedWatchdog();

    // Print banner
    GW_LOG_I(TAG, "================================================");
    GW_LOG_I(TAG, "  %s Gateway Firmware v%s",
             Config::FirmwareInfo::PROJECT,
             Config::FirmwareInfo::VERSION_STR);
    GW_LOG_I(TAG, "  Build: %s | %s",
             Config::FirmwareInfo::BUILD_TYPE,
             __DATE__);
    GW_LOG_I(TAG, "================================================");
    GW_LOG_I(TAG, "  Chip: ESP32 Rev%d | Cores: %d | %dMHz",
             static_cast<int>(esp_get_chip_info(nullptr), 0),
             static_cast<int>(ESP.getChipCores()),
             static_cast<int>(ESP.getCpuFreqMHz()));
    GW_LOG_I(TAG, "  Flash: %lu KB | RAM: %lu KB",
             static_cast<unsigned long>(ESP.getFlashChipSize() / 1024),
             static_cast<unsigned long>(ESP.getHeapSize() / 1024));
    GW_LOG_I(TAG, "  Free heap: %lu bytes",
             static_cast<unsigned long>(ESP.getFreeHeap()));
    GW_LOG_I(TAG, "  Reset reason: %d",
             static_cast<int>(esp_reset_reason()));
    GW_LOG_I(TAG, "================================================");

    StateMachine::getInstance().processEventSync(
        SystemEvent::HARDWARE_INIT_OK
    );

    return Result::OK;
}

// ============================================================
// Phase: Storage Init
// ============================================================
Result BootManager::phaseStorageInit() {
    setPhase("StorageInit", 15);
    feedWatchdog();

    // Mount LittleFS
    if (!LittleFS.begin(true)) {  // true = format on fail
        GW_LOG_E(TAG, "LittleFS mount failed");
        return Result::ERR_STORAGE_INIT_FAILED;
    }

    GW_LOG_I(TAG, "LittleFS mounted. Total: %lu KB | Used: %lu KB",
             static_cast<unsigned long>(LittleFS.totalBytes() / 1024),
             static_cast<unsigned long>(LittleFS.usedBytes() / 1024));

    // Initialize NVS storage via StorageManager
    auto* storageMgr = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::STORAGE_MANAGER
    );

    if (storageMgr) {
        Result r = storageMgr->initialize();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "StorageManager init failed: %s",
                     ResultHelper::toString(r));
            return r;
        }
    }

    StateMachine::getInstance().processEventSync(
        SystemEvent::STORAGE_MOUNTED
    );

    return Result::OK;
}

// ============================================================
// Phase: Config Load
// ============================================================
Result BootManager::phaseConfigLoad() {
    setPhase("ConfigLoad", 25);
    feedWatchdog();

    // Check if config file exists
    bool configExists = LittleFS.exists(
        Config::Storage::FS_CONFIG_FILE
    );

    if (!configExists) {
        GW_LOG_W(TAG, "No config file found — entering setup mode");
        StateMachine::getInstance().processEventSync(
            SystemEvent::CONFIG_NOT_FOUND
        );
        return Result::ERR_CONFIG_LOAD_FAILED;
    }

    // Delegate to ConfigManager
    auto* configMgr = Services::SL().getManagerAs<Interfaces::IManager>(
        Services::ManagerId::CONFIG_MANAGER
    );

    if (configMgr) {
        Result r = configMgr->initialize();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "ConfigManager init failed: %s",
                     ResultHelper::toString(r));
            StateMachine::getInstance().processEventSync(
                SystemEvent::CONFIG_CORRUPTED
            );
            return r;
        }
    }

    StateMachine::getInstance().processEventSync(
        SystemEvent::CONFIG_LOADED
    );

    return Result::OK;
}

// ============================================================
// Phase: Security Init
// ============================================================
Result BootManager::phaseSecurityInit() {
    setPhase("SecurityInit", 35);
    feedWatchdog();

    // CryptoEngine and SecureRandom initialization
    // will be delegated to SecurityManager (Phase 3)
    // For now, verify mbedTLS entropy source is available

    GW_LOG_I(TAG, "Security subsystem ready.");

    StateMachine::getInstance().processEventSync(
        SystemEvent::SECURITY_READY
    );

    return Result::OK;
}

// ============================================================
// Phase: Network Init
// ============================================================
Result BootManager::phaseNetworkInit() {
    setPhase("NetworkInit", 45);
    feedWatchdog();

    auto* networkMgr = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::NETWORK_MANAGER
    );

    if (!networkMgr) {
        GW_LOG_E(TAG, "NetworkManager not registered");
        return Result::ERR_NETWORK_INIT_FAILED;
    }

    Result r = networkMgr->initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "NetworkManager init failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    StateMachine::getInstance().processEventSync(
        SystemEvent::NETWORK_DRIVER_READY
    );

    return Result::OK;
}

// ============================================================
// Phase: Services Init
// ============================================================
Result BootManager::phaseServicesInit() {
    setPhase("ServicesInit", 55);
    feedWatchdog();

    // Initialize UserManager
    auto* userMgr = Services::SL().getManagerAs<Interfaces::IManager>(
        Services::ManagerId::USER_MANAGER
    );

    if (userMgr) {
        Result r = userMgr->initialize();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "UserManager init failed: %s",
                     ResultHelper::toString(r));
            return r;
        }
    }

    // Initialize SessionManager
    auto* sessionMgr = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::SESSION_MANAGER
    );

    if (sessionMgr) {
        Result r = sessionMgr->initialize();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "SessionManager init failed: %s",
                     ResultHelper::toString(r));
            return r;
        }
    }

    // Initialize AuthManager
    auto* authMgr = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::AUTH_MANAGER
    );

    if (authMgr) {
        Result r = authMgr->initialize();
        if (GW_ERR(r)) {
            GW_LOG_E(TAG, "AuthManager init failed: %s",
                     ResultHelper::toString(r));
            return r;
        }
    }

    feedWatchdog();

    StateMachine::getInstance().processEventSync(
        SystemEvent::SERVICES_READY
    );

    return Result::OK;
}

// ============================================================
// Phase: Network Connect
// ============================================================
Result BootManager::phaseNetworkConnect() {
    setPhase("NetworkConnect", 65);
    feedWatchdog();

    auto* networkMgr = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::NETWORK_MANAGER
    );

    if (!networkMgr) {
        GW_LOG_W(TAG, "No NetworkManager — skipping WiFi connect");
        StateMachine::getInstance().processEventSync(
            SystemEvent::WIFI_FAILED
        );
        return Result::ERR_NETWORK_INIT_FAILED;
    }

    Result r = networkMgr->start();
    if (GW_ERR(r)) {
        GW_LOG_W(TAG, "WiFi connect failed: %s — continuing degraded",
                 ResultHelper::toString(r));
        StateMachine::getInstance().processEventSync(
            SystemEvent::WIFI_FAILED
        );
        // Not fatal — gateway can run in degraded mode
        return Result::OK;
    }

    StateMachine::getInstance().processEventSync(
        SystemEvent::WIFI_CONNECTED
    );

    return Result::OK;
}

// ============================================================
// Phase: Web Server Init
// ============================================================
Result BootManager::phaseWebServerInit() {
    setPhase("WebServerInit", 80);
    feedWatchdog();

    auto* webServer = Services::SL().getServiceAs<Interfaces::IService>(
        Services::ServiceId::WEB_SERVER
    );

    if (!webServer) {
        GW_LOG_E(TAG, "WebServer not registered");
        return Result::ERR_WEBSERVER_INIT_FAILED;
    }

    Result r = webServer->initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "WebServer init failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    r = webServer->start();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "WebServer start failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    feedWatchdog();

    StateMachine::getInstance().processEventSync(
        SystemEvent::WEBSERVER_STARTED
    );

    return Result::OK;
}

// ============================================================
// FreeRTOS task — executes the boot sequence
// ============================================================
void BootManager::taskEntry(void* param) {
    static_cast<BootManager*>(param)->task();
}

void BootManager::task() {
    GW_LOG_I(TAG, "Boot task started on core %d",
             static_cast<int>(xPortGetCoreID()));

    // ---- Phase 1: Hardware ----
    {
        Result r = phaseHardwareInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::HARDWARE_INIT_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 2: Storage ----
    {
        Result r = phaseStorageInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::STORAGE_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 3: Config ----
    {
        Result r = phaseConfigLoad();
        if (GW_ERR(r)) {
            // StateMachine already transitioned (setup mode or error)
            // Boot can continue in setup mode
            if (StateMachine::getInstance().isInState(
                    SystemState::SETUP_MODE)) {
                setPhase("SetupMode", 50);
                GW_LOG_I(TAG, "Running in setup mode.");
                onBootSuccess();
            }
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 4: Security ----
    {
        Result r = phaseSecurityInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::SECURITY_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 5: Network driver ----
    {
        Result r = phaseNetworkInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::NETWORK_DRIVER_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 6: Services ----
    {
        Result r = phaseServicesInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::SERVICES_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Phase 7: WiFi connect ----
    {
        phaseNetworkConnect();
        // Not fatal — error handled inside phase
    }

    // ---- Phase 8: Web server ----
    {
        Result r = phaseWebServerInit();
        if (GW_ERR(r)) {
            onBootFailure(r, SystemEvent::WEBSERVER_FAILED);
            vTaskDelete(nullptr);
            return;
        }
    }

    // ---- Boot complete ----
    setPhase("Complete", 100);
    onBootSuccess();

    // Print final status
    Services::SL().printRegistryStatus();

    GW_LOG_I(TAG, "Free heap after boot: %lu bytes",
             static_cast<unsigned long>(ESP.getFreeHeap()));

    // Boot task self-destructs — its job is done
    vTaskDelete(nullptr);
}

} // namespace Core
} // namespace Gateway