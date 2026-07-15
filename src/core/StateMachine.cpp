// ============================================================
// StateMachine.cpp
// ============================================================

#include "StateMachine.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Core {

static constexpr const char* TAG = "StateMachine";

// ============================================================
// Transition table — stored in Flash (PROGMEM-safe with const)
// Each row: { FROM_STATE, EVENT, TO_STATE }
// Any (state, event) pair NOT in this table is REJECTED.
// ============================================================
const StateTransition StateMachine::s_transitionTable[] = {
    // ---- Boot sequence (linear happy path) ----
    { SystemState::POWER_ON,        SystemEvent::HARDWARE_INIT_OK,      SystemState::HARDWARE_INIT   },
    { SystemState::POWER_ON,        SystemEvent::HARDWARE_INIT_FAILED,  SystemState::CRITICAL_ERROR  },

    { SystemState::HARDWARE_INIT,   SystemEvent::HARDWARE_INIT_OK,      SystemState::STORAGE_INIT    },
    { SystemState::HARDWARE_INIT,   SystemEvent::HARDWARE_INIT_FAILED,  SystemState::CRITICAL_ERROR  },

    { SystemState::STORAGE_INIT,    SystemEvent::STORAGE_MOUNTED,       SystemState::CONFIG_LOAD     },
    { SystemState::STORAGE_INIT,    SystemEvent::STORAGE_FAILED,        SystemState::CRITICAL_ERROR  },

    { SystemState::CONFIG_LOAD,     SystemEvent::CONFIG_LOADED,         SystemState::SECURITY_INIT   },
    { SystemState::CONFIG_LOAD,     SystemEvent::CONFIG_NOT_FOUND,      SystemState::SETUP_MODE      },
    { SystemState::CONFIG_LOAD,     SystemEvent::CONFIG_CORRUPTED,      SystemState::ERROR           },

    { SystemState::SECURITY_INIT,   SystemEvent::SECURITY_READY,        SystemState::NETWORK_INIT    },
    { SystemState::SECURITY_INIT,   SystemEvent::SECURITY_FAILED,       SystemState::CRITICAL_ERROR  },

    { SystemState::NETWORK_INIT,    SystemEvent::NETWORK_DRIVER_READY,  SystemState::SERVICES_INIT   },
    { SystemState::NETWORK_INIT,    SystemEvent::NETWORK_DRIVER_FAILED, SystemState::CRITICAL_ERROR  },

    { SystemState::SERVICES_INIT,   SystemEvent::SERVICES_READY,        SystemState::NETWORK_CONNECT },
    { SystemState::SERVICES_INIT,   SystemEvent::SERVICES_FAILED,       SystemState::CRITICAL_ERROR  },

    { SystemState::NETWORK_CONNECT, SystemEvent::WIFI_CONNECTED,        SystemState::WEBSERVER_INIT  },
    { SystemState::NETWORK_CONNECT, SystemEvent::WIFI_FAILED,           SystemState::DEGRADED        },

    { SystemState::WEBSERVER_INIT,  SystemEvent::WEBSERVER_STARTED,     SystemState::RUNNING         },
    { SystemState::WEBSERVER_INIT,  SystemEvent::WEBSERVER_FAILED,      SystemState::ERROR           },

    // ---- Operational transitions ----
    { SystemState::RUNNING,         SystemEvent::HEALTH_DEGRADED,       SystemState::DEGRADED        },
    { SystemState::RUNNING,         SystemEvent::HEALTH_CRITICAL,       SystemState::CRITICAL_ERROR  },
    { SystemState::RUNNING,         SystemEvent::MAINTENANCE_ENTER,     SystemState::MAINTENANCE     },
    { SystemState::RUNNING,         SystemEvent::SHUTDOWN_REQUESTED,    SystemState::SHUTTING_DOWN   },
    { SystemState::RUNNING,         SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },

    { SystemState::DEGRADED,        SystemEvent::HEALTH_RECOVERED,      SystemState::RUNNING         },
    { SystemState::DEGRADED,        SystemEvent::HEALTH_CRITICAL,       SystemState::CRITICAL_ERROR  },
    { SystemState::DEGRADED,        SystemEvent::WIFI_CONNECTED,        SystemState::RUNNING         },
    { SystemState::DEGRADED,        SystemEvent::SHUTDOWN_REQUESTED,    SystemState::SHUTTING_DOWN   },
    { SystemState::DEGRADED,        SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },

    { SystemState::MAINTENANCE,     SystemEvent::MAINTENANCE_EXIT,      SystemState::RUNNING         },
    { SystemState::MAINTENANCE,     SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },
    { SystemState::MAINTENANCE,     SystemEvent::SHUTDOWN_REQUESTED,    SystemState::SHUTTING_DOWN   },

    // ---- Error recovery ----
    { SystemState::ERROR,           SystemEvent::ERROR_RECOVERED,       SystemState::RUNNING         },
    { SystemState::ERROR,           SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },
    { SystemState::ERROR,           SystemEvent::SHUTDOWN_REQUESTED,    SystemState::SHUTTING_DOWN   },
    { SystemState::ERROR,           SystemEvent::HEALTH_CRITICAL,       SystemState::CRITICAL_ERROR  },

    { SystemState::CRITICAL_ERROR,  SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },

    // ---- Setup mode ----
    { SystemState::SETUP_MODE,      SystemEvent::SETUP_COMPLETE,        SystemState::SECURITY_INIT   },
    { SystemState::SETUP_MODE,      SystemEvent::RESTART_REQUESTED,     SystemState::RESTARTING      },
    { SystemState::SETUP_MODE,      SystemEvent::SHUTDOWN_REQUESTED,    SystemState::SHUTTING_DOWN   },
};

const size_t StateMachine::s_transitionCount =
    sizeof(s_transitionTable) / sizeof(s_transitionTable[0]);

// ============================================================
// Singleton
// ============================================================
StateMachine& StateMachine::getInstance() noexcept {
    static StateMachine instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
StateMachine::StateMachine()
    : m_serviceState(Interfaces::ServiceState::UNINITIALIZED)
    , m_currentState(SystemState::POWER_ON)
    , m_previousState(SystemState::INVALID)
    , m_lastTransitionMs(0)
    , m_stateMutex(nullptr)
    , m_eventQueue(nullptr)
    , m_eventQueueBuffer{}
    , m_eventQueueStorage{}
    , m_taskHandle(nullptr)
    , m_taskTCB{}
    , m_observers{}
    , m_observerCount(0)
    , m_observerMutex(nullptr)
    , m_history{}
    , m_historyHead(0)
    , m_historyCount(0)
    , m_historyMutex(nullptr)
    , m_totalTransitions(0)
    , m_rejectedEvents(0)
{
}

StateMachine::~StateMachine() {
    (void)stop();
}

// ============================================================
// IService::initialize
// ============================================================
Result StateMachine::initialize() {
    if (m_serviceState != Interfaces::ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_serviceState = Interfaces::ServiceState::INITIALIZING;

    m_stateMutex = xSemaphoreCreateMutex();
    if (!m_stateMutex) {
        m_serviceState = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_observerMutex = xSemaphoreCreateMutex();
    if (!m_observerMutex) {
        m_serviceState = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_historyMutex = xSemaphoreCreateMutex();
    if (!m_historyMutex) {
        m_serviceState = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_eventQueue = xQueueCreateStatic(
        EVENT_QUEUE_LENGTH,
        sizeof(SystemEvent),
        m_eventQueueStorage,
        &m_eventQueueBuffer
    );

    if (!m_eventQueue) {
        m_serviceState = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_serviceState = Interfaces::ServiceState::STOPPED;

    GW_LOG_I(TAG, "Initialized. Transition table: %d entries.",
             static_cast<int>(s_transitionCount));

    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result StateMachine::start() {
    if (m_serviceState != Interfaces::ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    m_taskHandle = xTaskCreateStaticPinnedToCore(
        taskEntry,
        "StateMachine",
        Config::Tasks::STACK_STATE_MACHINE,
        this,
        Config::Tasks::PRIORITY_STATE_MACHINE,
        m_taskStack,
        &m_taskTCB,
        Config::Tasks::CORE_APPLICATION
    );

    if (!m_taskHandle) {
        m_serviceState = Interfaces::ServiceState::FAULTED;
        return Result::ERR_OPERATION_FAILED;
    }

    m_serviceState = Interfaces::ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started. Initial state: %s",
             systemStateToString(m_currentState));

    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result StateMachine::stop() {
    if (m_serviceState != Interfaces::ServiceState::RUNNING &&
        m_serviceState != Interfaces::ServiceState::PAUSED) {
        return Result::ERR_INVALID_STATE;
    }

    m_serviceState = Interfaces::ServiceState::STOPPING;

    if (m_taskHandle) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    m_serviceState = Interfaces::ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool StateMachine::isHealthy() const {
    return m_serviceState == Interfaces::ServiceState::RUNNING &&
           m_currentState != SystemState::CRITICAL_ERROR &&
           m_currentState != SystemState::INVALID;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport StateMachine::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    report.status = isHealthy()
        ? Interfaces::HealthStatus::HEALTHY
        : Interfaces::HealthStatus::CRITICAL;

    snprintf(report.detail, sizeof(report.detail),
             "State:%s Transitions:%lu Rejected:%lu",
             systemStateToString(m_currentState),
             static_cast<unsigned long>(m_totalTransitions),
             static_cast<unsigned long>(m_rejectedEvents));

    return report;
}

// ============================================================
// postEvent — non-blocking, safe from any context
// ============================================================
Result StateMachine::postEvent(SystemEvent event) {
    if (!m_eventQueue) {
        return Result::ERR_NOT_INITIALIZED;
    }

    BaseType_t sent;

    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        sent = xQueueSendToBackFromISR(m_eventQueue, &event, &woken);
        if (woken) portYIELD_FROM_ISR();
    } else {
        sent = xQueueSendToBack(m_eventQueue, &event, 0);
    }

    if (sent != pdTRUE) {
        GW_LOG_W(TAG, "Event queue full! Dropping event: %s",
                 systemEventToString(event));
        return Result::ERR_QUEUE_FULL;
    }

    return Result::OK;
}

// ============================================================
// processEventSync — immediate, deterministic
// ============================================================
Result StateMachine::processEventSync(SystemEvent event) {
    return executeTransition(event);
}

// ============================================================
// executeTransition — core transition logic
// ============================================================
Result StateMachine::executeTransition(SystemEvent event) {
    if (xSemaphoreTake(m_stateMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    SystemState current = m_currentState;

    const StateTransition* transition = findTransition(current, event);

    if (transition == nullptr) {
        m_rejectedEvents++;
        xSemaphoreGive(m_stateMutex);

        GW_LOG_W(TAG, "No transition from %s on event %s",
                 systemStateToString(current),
                 systemEventToString(event));

        return Result::ERR_STATE_TRANSITION_DENIED;
    }

    SystemState nextState = transition->to;

    // Exit current state
    runExitAction(current);

    // Update state
    m_previousState      = current;
    m_currentState       = nextState;
    m_lastTransitionMs   = static_cast<uint32_t>(millis());
    m_totalTransitions++;

    xSemaphoreGive(m_stateMutex);

    // Record before notifications (non-blocking)
    recordTransition(current, nextState, event);

    GW_LOG_I(TAG, "Transition: %s -[%s]-> %s",
             systemStateToString(current),
             systemEventToString(event),
             systemStateToString(nextState));

    // Enter new state
    runEntryAction(nextState);

    // Notify all observers
    notifyObservers(current, nextState, event);

    // Publish to EventBus
    Services::EventBus::getInstance().publish(
        Interfaces::EventType::SYSTEM_STATE_CHANGED,
        static_cast<uint32_t>(current),
        static_cast<uint32_t>(nextState),
        static_cast<uint32_t>(event)
    );

    return Result::OK;
}

// ============================================================
// findTransition — linear search over ROM table
// O(n) where n = s_transitionCount — acceptable for this size
// ============================================================
const StateTransition* StateMachine::findTransition(
    SystemState current,
    SystemEvent event)
{
    for (size_t i = 0; i < s_transitionCount; ++i) {
        if (s_transitionTable[i].from  == current &&
            s_transitionTable[i].event == event) {
            return &s_transitionTable[i];
        }
    }
    return nullptr;
}

// ============================================================
// Entry actions — called when entering a state
// ============================================================
void StateMachine::runEntryAction(SystemState state) {
    switch (state) {
        case SystemState::CRITICAL_ERROR:
            GW_LOG_C(TAG, "*** CRITICAL ERROR STATE ENTERED ***");
            // Trigger automatic restart after delay
            Services::EventBus::getInstance().publish(
                Interfaces::EventType::SYSTEM_ERROR,
                static_cast<uint32_t>(state)
            );
            break;

        case SystemState::RUNNING:
            GW_LOG_I(TAG, "*** GATEWAY FULLY OPERATIONAL ***");
            Services::EventBus::getInstance().publish(
                Interfaces::EventType::SYSTEM_BOOT_COMPLETE
            );
            break;

        case SystemState::SETUP_MODE:
            GW_LOG_I(TAG, "Entering setup mode (AP).");
            break;

        case SystemState::DEGRADED:
            GW_LOG_W(TAG, "System degraded — limited functionality.");
            break;

        case SystemState::RESTARTING:
            GW_LOG_I(TAG, "Restart requested — cleaning up...");
            Services::EventBus::getInstance().publish(
                Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED
            );
            break;

        case SystemState::SHUTTING_DOWN:
            GW_LOG_I(TAG, "Shutdown requested — cleaning up...");
            Services::EventBus::getInstance().publish(
                Interfaces::EventType::SYSTEM_SHUTDOWN_REQUESTED
            );
            break;

        default:
            break;
    }
}

// ============================================================
// Exit actions — called when leaving a state
// ============================================================
void StateMachine::runExitAction(SystemState state) {
    switch (state) {
        case SystemState::MAINTENANCE:
            GW_LOG_I(TAG, "Exiting maintenance mode.");
            break;

        case SystemState::SETUP_MODE:
            GW_LOG_I(TAG, "Setup mode complete.");
            break;

        default:
            break;
    }
}

// ============================================================
// Observer notification
// ============================================================
void StateMachine::notifyObservers(SystemState from,
                                    SystemState to,
                                    SystemEvent event) {
    if (xSemaphoreTake(m_observerMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    // Local snapshot to minimize lock time
    StateChangeCallback localCbs[MAX_OBSERVERS];
    uint8_t             localCount = m_observerCount;
    for (uint8_t i = 0; i < localCount; ++i) {
        localCbs[i] = m_observers[i];
    }

    xSemaphoreGive(m_observerMutex);

    for (uint8_t i = 0; i < localCount; ++i) {
        if (localCbs[i]) {
            localCbs[i](from, to, event);
        }
    }
}

// ============================================================
// recordTransition — ring buffer history
// ============================================================
void StateMachine::recordTransition(SystemState from,
                                     SystemState to,
                                     SystemEvent event) {
    if (xSemaphoreTake(m_historyMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    m_history[m_historyHead] = {
        from, to, event,
        static_cast<uint32_t>(millis())
    };

    m_historyHead = (m_historyHead + 1) % HISTORY_DEPTH;
    if (m_historyCount < HISTORY_DEPTH) {
        m_historyCount++;
    }

    xSemaphoreGive(m_historyMutex);
}

// ============================================================
// addObserver
// ============================================================
Result StateMachine::addObserver(StateChangeCallback callback) {
    if (!callback) return Result::ERR_INVALID_ARGUMENT;

    if (xSemaphoreTake(m_observerMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result r = Result::OK;

    if (m_observerCount >= MAX_OBSERVERS) {
        r = Result::ERR_MAX_CAPACITY;
    } else {
        m_observers[m_observerCount++] = std::move(callback);
    }

    xSemaphoreGive(m_observerMutex);
    return r;
}

// ============================================================
// State query methods
// ============================================================
SystemState StateMachine::getCurrentState() const noexcept {
    if (xSemaphoreTake(m_stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        SystemState s = m_currentState;
        xSemaphoreGive(m_stateMutex);
        return s;
    }
    return m_currentState; // Best effort if mutex fails
}

SystemState StateMachine::getPreviousState() const noexcept {
    return m_previousState;
}

bool StateMachine::isInState(SystemState s) const noexcept {
    return getCurrentState() == s;
}

bool StateMachine::isOperational() const noexcept {
    SystemState s = getCurrentState();
    return s == SystemState::RUNNING  ||
           s == SystemState::DEGRADED ||
           s == SystemState::MAINTENANCE;
}

// ============================================================
// getHistory
// ============================================================
uint8_t StateMachine::getHistory(TransitionRecord* outBuffer,
                                  uint8_t          bufferSize) const {
    if (!outBuffer || bufferSize == 0) return 0;

    if (xSemaphoreTake(m_historyMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }

    uint8_t count  = (m_historyCount < bufferSize) ? m_historyCount : bufferSize;
    uint8_t start  = (m_historyHead - m_historyCount + HISTORY_DEPTH) % HISTORY_DEPTH;

    for (uint8_t i = 0; i < count; ++i) {
        outBuffer[i] = m_history[(start + i) % HISTORY_DEPTH];
    }

    xSemaphoreGive(m_historyMutex);
    return count;
}

// ============================================================
// getState (IService)
// ============================================================
Interfaces::ServiceState StateMachine::getState() const {
    return m_serviceState;
}

// ============================================================
// FreeRTOS task
// ============================================================
void StateMachine::taskEntry(void* param) {
    static_cast<StateMachine*>(param)->task();
}

void StateMachine::task() {
    GW_LOG_I(TAG, "Task running on core %d",
             static_cast<int>(xPortGetCoreID()));

    SystemEvent event;

    while (true) {
        if (xQueueReceive(m_eventQueue, &event,
                          pdMS_TO_TICKS(5000)) == pdTRUE) {
            executeTransition(event);
        }

        // Periodic: auto-restart from critical error after 10s
        if (m_currentState == SystemState::CRITICAL_ERROR) {
            uint32_t now = static_cast<uint32_t>(millis());
            if (now - m_lastTransitionMs > 10000) {
                GW_LOG_C(TAG, "Critical error timeout — restarting.");
                esp_restart();
            }
        }

        // Periodic: auto-restart in RESTARTING state after 2s
        if (m_currentState == SystemState::RESTARTING) {
            uint32_t now = static_cast<uint32_t>(millis());
            if (now - m_lastTransitionMs > 2000) {
                GW_LOG_I(TAG, "Restarting now.");
                esp_restart();
            }
        }
    }
}

} // namespace Core
} // namespace Gateway