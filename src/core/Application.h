// ============================================================
// Application.h
// The top-level application class.
//
// Application owns:
//   - All service instances
//   - All manager instances
//   - ServiceLocator registration
//   - Boot sequence orchestration
//   - Graceful shutdown
//
// This is the only class that knows about ALL other classes.
// Everything else depends only on interfaces.
//
// begin() is called from Arduino setup().
// No logic lives in Arduino loop().
// ============================================================

#pragma once

#ifndef APPLICATION_H
#define APPLICATION_H

#include "ErrorCodes.h"
#include "SystemState.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../services/ServiceLocator.h"
#include "../interfaces/IEventListener.h"

#include <cstdint>

namespace Gateway {

// ============================================================
// Application
// Singleton — owns the entire firmware lifecycle.
// ============================================================
class Application final
    : public Interfaces::IEventListener
{
public:
    // --------------------------------------------------------
    // Singleton access
    // --------------------------------------------------------
    [[nodiscard]] static Application& getInstance() noexcept;

    // --------------------------------------------------------
    // Called from Arduino setup()
    // Initializes and starts the entire system.
    // Returns immediately — system runs via FreeRTOS tasks.
    // --------------------------------------------------------
    void begin();

    // --------------------------------------------------------
    // Called from Arduino loop() — intentionally minimal
    // --------------------------------------------------------
    void tick();

    // --------------------------------------------------------
    // IEventListener
    // --------------------------------------------------------
    void onEvent(const Interfaces::Event& event) override;

    [[nodiscard]] const Interfaces::EventType*
    getSubscribedEvents(size_t& outCount) const override;

    [[nodiscard]] const char*
    getListenerName() const override { return "Application"; }

    // --------------------------------------------------------
    // Graceful shutdown
    // --------------------------------------------------------
    void shutdown();
    void restart();

private:
    Application();
    ~Application() = default;

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    // --------------------------------------------------------
    // Initialization stages
    // --------------------------------------------------------
    Result initializeFoundation();
    Result initializeServices();
    Result startFoundationServices();

    // --------------------------------------------------------
    // Service instances — owned here, referenced everywhere else
    // through ServiceLocator.
    //
    // Logger and EventBus are singletons — no ownership here.
    // All others are allocated as static local or member objects.
    // --------------------------------------------------------
    void createServiceInstances();

    // --------------------------------------------------------
    // Event subscriptions
    // --------------------------------------------------------
    static const Interfaces::EventType s_subscribedEvents[];
    static const size_t                s_subscribedEventCount;

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------
    bool m_initialized;
};

} // namespace Gateway

#endif // APPLICATION_H