// ============================================================
// StateMachine.h
// Deterministic finite state machine for the gateway.
//
// Design decisions:
//   - Transition table driven: all valid transitions defined
//     in a compile-time array. Invalid transitions are
//     explicitly rejected — no implicit state changes.
//   - Observer pattern: external components can register
//     callbacks for state change notifications.
//   - Thread-safe: mutex protects state and transition logic.
//   - FreeRTOS task: processes event queue asynchronously.
//   - Entry/Exit actions per state via callback table.
//   - History: maintains last N state transitions for debug.
// ============================================================

#pragma once

#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include "SystemState.h"
#include "ErrorCodes.h"
#include "../interfaces/IService.h"
#include "../interfaces/IHealthCheck.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <functional>

namespace Gateway {
namespace Core {

// ============================================================
// State change observer callback type
// ============================================================
using StateChangeCallback = std::function<void(SystemState from,
                                               SystemState to,
                                               SystemEvent event)>;

// ============================================================
// Transition history entry
// ============================================================
struct TransitionRecord {
    SystemState from;
    SystemState to;
    SystemEvent event;
    uint32_t    timestampMs;
};

// ============================================================
// StateMachine
// ============================================================
class StateMachine final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    static constexpr uint8_t MAX_OBSERVERS        = 8;
    static constexpr uint8_t HISTORY_DEPTH        = 16;
    static constexpr uint8_t EVENT_QUEUE_LENGTH   = 12;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static StateMachine& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()        override;
    [[nodiscard]] Result       start()             override;
    [[nodiscard]] Result       stop()              override;
    [[nodiscard]] Interfaces::ServiceState getState()  const   override;
    [[nodiscard]] const char*  getName()   const   override { return "StateMachine"; }
    [[nodiscard]] bool         isHealthy() const   override;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "StateMachine"; }

    // --------------------------------------------------------
    // Core API
    // --------------------------------------------------------

    // Post an event to be processed asynchronously by the task.
    // Safe to call from any context.
    [[nodiscard]] Result postEvent(SystemEvent event);

    // Process event immediately (synchronous — use with care).
    // Only call from StateMachine task or during single-threaded boot.
    [[nodiscard]] Result processEventSync(SystemEvent event);

    // --------------------------------------------------------
    // State query — thread-safe
    // --------------------------------------------------------
    [[nodiscard]] SystemState getCurrentState() const noexcept;
    [[nodiscard]] SystemState getPreviousState() const noexcept;
    [[nodiscard]] bool        isInState(SystemState s) const noexcept;

    // Returns true if system is in any operational state
    [[nodiscard]] bool isOperational() const noexcept;

    // --------------------------------------------------------
    // Observer registration
    // --------------------------------------------------------
    [[nodiscard]] Result addObserver(StateChangeCallback callback);

    // --------------------------------------------------------
    // Transition history (for diagnostics)
    // --------------------------------------------------------
    uint8_t getHistory(TransitionRecord* outBuffer,
                       uint8_t          bufferSize) const;

private:
    StateMachine();
    ~StateMachine();

    StateMachine(const StateMachine&)            = delete;
    StateMachine& operator=(const StateMachine&) = delete;

    // --------------------------------------------------------
    // Internal transition logic
    // --------------------------------------------------------
    Result executeTransition(SystemEvent event);
    void   runEntryAction(SystemState state);
    void   runExitAction(SystemState state);
    void   notifyObservers(SystemState from,
                           SystemState to,
                           SystemEvent event);
    void   recordTransition(SystemState from,
                            SystemState to,
                            SystemEvent event);

    // --------------------------------------------------------
    // FreeRTOS task
    // --------------------------------------------------------
    static void taskEntry(void* param);
    void        task();

    // --------------------------------------------------------
    // Transition table helpers
    // --------------------------------------------------------
    static const StateTransition* findTransition(
        SystemState current,
        SystemEvent event
    );

    // --------------------------------------------------------
    // Static transition table (ROM — no RAM cost)
    // --------------------------------------------------------
    static const StateTransition s_transitionTable[];
    static const size_t          s_transitionCount;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState    m_serviceState;
    SystemState                 m_currentState;
    SystemState                 m_previousState;
    uint32_t                    m_lastTransitionMs;

    // FreeRTOS primitives — statically allocated
    SemaphoreHandle_t           m_stateMutex;

    QueueHandle_t               m_eventQueue;
    StaticQueue_t               m_eventQueueBuffer;
    uint8_t                     m_eventQueueStorage[
        EVENT_QUEUE_LENGTH * sizeof(SystemEvent)
    ];

    TaskHandle_t                m_taskHandle;
    StaticTask_t                m_taskTCB;
    StackType_t                 m_taskStack[ Config::Tasks::STACK_STATE_MACHINE ];

    // Observers
    StateChangeCallback         m_observers[MAX_OBSERVERS];
    uint8_t                     m_observerCount;
    SemaphoreHandle_t           m_observerMutex;

    // History ring buffer
    TransitionRecord            m_history[HISTORY_DEPTH];
    uint8_t                     m_historyHead;
    uint8_t                     m_historyCount;
    mutable SemaphoreHandle_t   m_historyMutex;

    // Statistics
    uint32_t                    m_totalTransitions;
    uint32_t                    m_rejectedEvents;
};

} // namespace Core
} // namespace Gateway

#endif // STATE_MACHINE_H