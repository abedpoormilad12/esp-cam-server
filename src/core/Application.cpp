// ============================================================
// Application.cpp
// ============================================================

#include "Application.h"
#include "BootManager.h"
#include "StateMachine.h"

#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../services/ServiceLocator.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

namespace Gateway {

static constexpr const char* TAG = "Application";

// ============================================================
// Events this class subscribes to
// ============================================================
const Interfaces::EventType Application::s_subscribedEvents[] = {
    Interfaces::EventType::SYSTEM_BOOT_COMPLETE,
    Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED,
    Interfaces::EventType::SYSTEM_ERROR,
    Interfaces::EventType::HEALTH_CRITICAL,
};

const size_t Application::s_subscribedEventCount =
    sizeof(s_subscribedEvents) / sizeof(s_subscribedEvents[0]);

// ============================================================
// Singleton
// ============================================================
Application& Application::getInstance() noexcept {
    static Application instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
Application::Application()
    : m_initialized(false)
{
}

// ============================================================
// begin() — called from Arduino setup()
// ============================================================
void Application::begin() {
    // ---- Step 1: Initialize Watchdog ----
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = Config::Hardware::WATCHDOG_TIMEOUT_S * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_task_wdt_reconfigure(&wdt_cfg);
    esp_task_wdt_add(nullptr);

    // ---- Step 2: Initialize Foundation (Logger + EventBus) ----
    Result r = initializeFoundation();
    if (GW_ERR(r)) {
        // Can't log — Serial may not be ready — busy-loop
        while (true) {
            digitalWrite(Config::Hardware::LED_STATUS_PIN, HIGH);
            delay(100);
            digitalWrite(Config::Hardware::LED_STATUS_PIN, LOW);
            delay(100);
        }
    }

    // ---- Step 3: Create + register all service instances ----
    createServiceInstances();

    // ---- Step 4: Start foundation services ----
    r = startFoundationServices();
    if (GW_ERR(r)) {
        GW_LOG_C(TAG, "Foundation services failed to start: %s",
                 ResultHelper::toString(r));
        esp_restart();
    }

    // ---- Step 5: Subscribe to EventBus ----
    Services::EventBus::getInstance().subscribe(this);

    // ---- Step 6: Initialize + start StateMachine ----
    r = Core::StateMachine::getInstance().initialize();
    if (GW_ERR(r)) {
        GW_LOG_C(TAG, "StateMachine init failed: %s",
                 ResultHelper::toString(r));
        esp_restart();
    }

    r = Core::StateMachine::getInstance().start();
    if (GW_ERR(r)) {
        GW_LOG_C(TAG, "StateMachine start failed: %s",
                 ResultHelper::toString(r));
        esp_restart();
    }

    // ---- Step 7: Lock service registration ----
    Services::SL().lockRegistration();

    // ---- Step 8: Initialize + start BootManager ----
    r = Core::BootManager::getInstance().initialize();
    if (GW_ERR(r)) {
        GW_LOG_C(TAG, "BootManager init failed: %s",
                 ResultHelper::toString(r));
        esp_restart();
    }

    r = Core::BootManager::getInstance().start();
    if (GW_ERR(r)) {
        GW_LOG_C(TAG, "BootManager start failed: %s",
                 ResultHelper::toString(r));
        esp_restart();
    }

    m_initialized = true;

    GW_LOG_I(TAG, "Application::begin() complete — "
             "control transferred to FreeRTOS tasks.");

    // setup() returns — FreeRTOS scheduler takes over
}

// ============================================================
// tick() — called from Arduino loop()
// Intentionally minimal: just feed WDT and yield.
// ============================================================
void Application::tick() {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// ============================================================
// initializeFoundation
// Logger and ServiceLocator must be ready before anything else.
// ============================================================
Result Application::initializeFoundation() {
    // Initialize ServiceLocator first
    Result r = Services::ServiceLocator::getInstance().initialize();
    if (GW_ERR(r)) return r;

    // Initialize Logger — Serial backend
    r = Services::Logger::getInstance().initialize();
    if (GW_ERR(r)) return r;

    // Add Serial backend immediately so boot messages appear
    static Services::SerialLogBackend serialBackend(true);
    r = Services::Logger::getInstance().addBackend(&serialBackend);
    if (GW_ERR(r)) return r;

    // Start Logger task
    r = Services::Logger::getInstance().start();
    if (GW_ERR(r)) return r;

    // Initialize EventBus
    r = Services::EventBus::getInstance().initialize();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "EventBus init failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    return Result::OK;
}

// ============================================================
// createServiceInstances
// All services are created as static locals here.
// This is intentional: static storage avoids heap fragmentation
// while still being encapsulated in this function.
// ServiceLocator holds non-owning pointers — lifetime is the
// entire firmware run.
// ============================================================
void Application::createServiceInstances() {
    using namespace Services;

    // Register Logger (already initialized)
    SL().registerService(ServiceId::LOGGER,
                          &Logger::getInstance());

    // Register EventBus (already initialized)
    SL().registerService(ServiceId::EVENT_BUS,
                          &EventBus::getInstance());

    // Future services will be instantiated and registered here
    // as their modules are implemented in subsequent phases:
    //
    // Phase 4: StorageManager, ConfigManager
    // Phase 5: NetworkManager
    // Phase 6: AuthManager, SessionManager, UserManager
    // Phase 7: WebServer
    // Phase 9: HealthMonitor, DeviceRegistry (skeleton)
    //
    // Example pattern for each:
    //   static ConcreteService serviceInstance;
    //   SL().registerService(ServiceId::XXX, &serviceInstance);

    GW_LOG_I(TAG, "Service instances created and registered.");
}

// ============================================================
// startFoundationServices
// ============================================================
Result Application::startFoundationServices() {
    // Start EventBus — Logger is already started
    Result r = Services::EventBus::getInstance().start();
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "EventBus start failed: %s",
                 ResultHelper::toString(r));
        return r;
    }

    GW_LOG_I(TAG, "Foundation services started.");
    return Result::OK;
}

// ============================================================
// IEventListener::onEvent
// ============================================================
void Application::onEvent(const Interfaces::Event& event) {
    switch (event.type) {
        case Interfaces::EventType::SYSTEM_BOOT_COMPLETE:
            GW_LOG_I(TAG, "System boot complete event received.");
            // Turn off LED or signal operational status
            digitalWrite(Config::Hardware::LED_STATUS_PIN, LOW);
            break;

        case Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED:
            GW_LOG_I(TAG, "Shutdown requested via event.");
            shutdown();
            break;

        case Interfaces::EventType::SYSTEM_ERROR:
            GW_LOG_E(TAG, "System error event received. Data: %lu",
                     static_cast<unsigned long>(event.data[0]));
            break;

        case Interfaces::EventType::HEALTH_CRITICAL:
            GW_LOG_C(TAG, "Critical health event — initiating restart.");
            restart();
            break;

        default:
            break;
    }
}

// ============================================================
// getSubscribedEvents
// ============================================================
const Interfaces::EventType*
Application::getSubscribedEvents(size_t& outCount) const {
    outCount = s_subscribedEventCount;
    return s_subscribedEvents;
}

// ============================================================
// shutdown
// ============================================================
void Application::shutdown() {
    GW_LOG_I(TAG, "Initiating graceful shutdown...");

    Services::SL().stopAllServices();

    GW_LOG_I(TAG, "All services stopped. Halting.");
    Services::Logger::getInstance().flush(3000);

    // On embedded — shutdown means halt or deep sleep
    esp_deep_sleep_start();
}

// ============================================================
// restart
// ============================================================
void Application::restart() {
    GW_LOG_I(TAG, "Initiating restart...");

    Services::SL().stopAllServices();
    Services::Logger::getInstance().flush(2000);

    esp_restart();
}

} // namespace Gateway