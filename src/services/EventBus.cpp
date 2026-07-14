// ============================================================
// EventBus.cpp
// ============================================================

#include "EventBus.h"
#include "Logger.h"

#include <Arduino.h>
#include <cstring>

namespace Gateway {
namespace Services {

static constexpr const char* TAG = "EventBus";

// ============================================================
// Singleton
// ============================================================
EventBus& EventBus::getInstance() noexcept {
    static EventBus instance;
    return instance;
}

EventBus::EventBus()
    : m_state(ServiceState::UNINITIALIZED)
    , m_subscribers{}
    , m_subscriberCount(0)
    , m_subscriberMutex(nullptr)
    , m_queue(nullptr)
    , m_queueBuffer{}
    , m_queueStorage{}
    , m_taskHandle(nullptr)
    , m_taskTCB{}
    , m_taskStack{}
    , m_statsMutex(nullptr)
    , m_stats{}
{
}

EventBus::~EventBus() {
    stop();
}

// ============================================================
// IService::initialize
// ============================================================
Result EventBus::initialize() {
    if (m_state != ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = ServiceState::INITIALIZING;

    // Create queue with static storage
    m_queue = xQueueCreateStatic(
        QUEUE_LENGTH,
        sizeof(Event),
        m_queueStorage,
        &m_queueBuffer
    );

    if (m_queue == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    // Create mutexes
    m_subscriberMutex = xSemaphoreCreateMutex();
    if (m_subscriberMutex == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_statsMutex = xSemaphoreCreateMutex();
    if (m_statsMutex == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OUT_OF_MEMORY;
    }

    m_stats = Stats{};
    m_state = ServiceState::STOPPED;

    GW_LOG_I(TAG, "Initialized. Queue depth: %d, Max subscribers: %d",
             QUEUE_LENGTH, static_cast<int>(sizeof(m_subscribers) /
             sizeof(m_subscribers[0])));

    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result EventBus::start() {
    if (m_state != ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    m_taskHandle = xTaskCreateStaticPinnedToCore(
        eventBusTaskEntry,
        "EventBus",
        TASK_STACK_WORDS,
        this,
        Config::Tasks::PRIORITY_EVENT_BUS,
        m_taskStack,
        &m_taskTCB,
        Config::Tasks::CORE_APPLICATION
    );

    if (m_taskHandle == nullptr) {
        m_state = ServiceState::FAULTED;
        return Result::ERR_OPERATION_FAILED;
    }

    m_state = ServiceState::RUNNING;
    GW_LOG_I(TAG, "Started.");
    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result EventBus::stop() {
    if (m_state != ServiceState::RUNNING &&
        m_state != ServiceState::PAUSED) {
        return Result::ERR_INVALID_STATE;
    }

    m_state = ServiceState::STOPPING;

    if (m_taskHandle != nullptr) {
        vTaskDelete(m_taskHandle);
        m_taskHandle = nullptr;
    }

    m_state = ServiceState::STOPPED;
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool EventBus::isHealthy() const {
    if (m_state != ServiceState::RUNNING) return false;
    if (m_queue == nullptr)               return false;

    Stats s = getStats();
    if (s.totalPublished > 50) {
        uint32_t dropPct = (s.totalDropped * 100) / s.totalPublished;
        if (dropPct > 5) return false;
    }

    return true;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport EventBus::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    Stats s = getStats();

    if (!isHealthy()) {
        report.status = Interfaces::HealthStatus::DEGRADED;
        snprintf(report.detail, sizeof(report.detail),
                 "Dropped:%lu Subs:%d",
                 static_cast<unsigned long>(s.totalDropped),
                 static_cast<int>(s.subscriberCount));
    } else {
        report.status = Interfaces::HealthStatus::HEALTHY;
        snprintf(report.detail, sizeof(report.detail),
                 "Published:%lu Subs:%d",
                 static_cast<unsigned long>(s.totalPublished),
                 static_cast<int>(s.subscriberCount));
    }

    return report;
}

// ============================================================
// subscribe
// ============================================================
Result EventBus::subscribe(IEventListener* listener) {
    if (listener == nullptr) {
        return Result::ERR_NULL_POINTER;
    }

    if (xSemaphoreTake(m_subscriberMutex,
                       pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result result = Result::OK;

    // Check capacity
    constexpr uint8_t MAX_SUBS = sizeof(m_subscribers) /
                                 sizeof(m_subscribers[0]);

    if (m_subscriberCount >= MAX_SUBS) {
        result = Result::ERR_MAX_CAPACITY;
    } else {
        // Check for duplicate
        bool found = false;
        for (uint8_t i = 0; i < m_subscriberCount; ++i) {
            if (m_subscribers[i] == listener) {
                found  = true;
                result = Result::ERR_ALREADY_EXISTS;
                break;
            }
        }

        if (!found) {
            m_subscribers[m_subscriberCount++] = listener;

            // Update stats
            if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
                m_stats.subscriberCount = m_subscriberCount;
                xSemaphoreGive(m_statsMutex);
            }

            GW_LOG_D(TAG, "Subscriber added: %s (total: %d)",
                     listener->getListenerName(),
                     static_cast<int>(m_subscriberCount));
        }
    }

    xSemaphoreGive(m_subscriberMutex);
    return result;
}

// ============================================================
// unsubscribe
// ============================================================
Result EventBus::unsubscribe(IEventListener* listener) {
    if (listener == nullptr) {
        return Result::ERR_NULL_POINTER;
    }

    if (xSemaphoreTake(m_subscriberMutex,
                       pdMS_TO_TICKS(100)) != pdTRUE) {
        return Result::ERR_TIMEOUT;
    }

    Result result = Result::ERR_NOT_FOUND;

    for (uint8_t i = 0; i < m_subscriberCount; ++i) {
        if (m_subscribers[i] == listener) {
            // Compact the array
            for (uint8_t j = i; j < m_subscriberCount - 1; ++j) {
                m_subscribers[j] = m_subscribers[j + 1];
            }
            m_subscribers[--m_subscriberCount] = nullptr;

            if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
                m_stats.subscriberCount = m_subscriberCount;
                xSemaphoreGive(m_statsMutex);
            }

            GW_LOG_D(TAG, "Subscriber removed: %s",
                     listener->getListenerName());

            result = Result::OK;
            break;
        }
    }

    xSemaphoreGive(m_subscriberMutex);
    return result;
}

// ============================================================
// publish (Event)
// ============================================================
Result EventBus::publish(const Event& event) {
    if (m_queue == nullptr) {
        return Result::ERR_NOT_INITIALIZED;
    }

    BaseType_t queued;

    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        queued = xQueueSendToBackFromISR(m_queue, &event, &woken);
        if (woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        queued = xQueueSendToBack(m_queue, &event, 0);
    }

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalPublished++;
        if (queued != pdTRUE) {
            m_stats.totalDropped++;
        }
        UBaseType_t wm = uxQueueMessagesWaiting(m_queue);
        if (wm > m_stats.queueHighWaterMark) {
            m_stats.queueHighWaterMark = wm;
        }
        xSemaphoreGive(m_statsMutex);
    }

    return (queued == pdTRUE) ? Result::OK : Result::ERR_QUEUE_FULL;
}

// ============================================================
// publish (EventType + message)
// ============================================================
Result EventBus::publish(EventType type, const char* message) {
    Event event(type, static_cast<uint32_t>(millis()));
    if (message != nullptr) {
        strncpy(event.message, message, sizeof(event.message) - 1);
        event.message[sizeof(event.message) - 1] = '\0';
    }
    return publish(event);
}

// ============================================================
// publish (EventType + data)
// ============================================================
Result EventBus::publish(EventType type,
                         uint32_t  data0,
                         uint32_t  data1,
                         uint32_t  data2,
                         uint32_t  data3) {
    Event event(type, static_cast<uint32_t>(millis()));
    event.data[0] = data0;
    event.data[1] = data1;
    event.data[2] = data2;
    event.data[3] = data3;
    return publish(event);
}

// ============================================================
// publishSync
// ============================================================
void EventBus::publishSync(const Event& event) {
    dispatchEvent(event);
}

// ============================================================
// shouldDeliver
// Checks if a listener wants a specific event type.
// ============================================================
bool EventBus::shouldDeliver(IEventListener* listener,
                              const Event&    event) const {
    size_t count = 0;
    const EventType* types = listener->getSubscribedEvents(count);

    // nullptr means "subscribe to all"
    if (types == nullptr || count == 0) {
        return true;
    }

    for (size_t i = 0; i < count; ++i) {
        if (types[i] == event.type) {
            return true;
        }
    }

    return false;
}

// ============================================================
// dispatchEvent — called from EventBus task
// ============================================================
void EventBus::dispatchEvent(const Event& event) {
    if (xSemaphoreTake(m_subscriberMutex,
                       pdMS_TO_TICKS(
                           Config::EventBus::DISPATCH_TIMEOUT_MS
                       )) != pdTRUE) {
        GW_LOG_W(TAG, "Dispatch timeout acquiring subscriber lock");
        return;
    }

    // Take a local snapshot of subscribers to minimize lock time
    IEventListener* localSubs[32];
    uint8_t         localCount = m_subscriberCount;

    for (uint8_t i = 0; i < localCount; ++i) {
        localSubs[i] = m_subscribers[i];
    }

    xSemaphoreGive(m_subscriberMutex);

    // Dispatch to each interested subscriber
    for (uint8_t i = 0; i < localCount; ++i) {
        if (localSubs[i] != nullptr &&
            shouldDeliver(localSubs[i], event)) {
            localSubs[i]->onEvent(event);
        }
    }

    if (xSemaphoreTake(m_statsMutex, 0) == pdTRUE) {
        m_stats.totalDispatched++;
        xSemaphoreGive(m_statsMutex);
    }
}

// ============================================================
// FreeRTOS task
// ============================================================
void EventBus::eventBusTaskEntry(void* param) {
    static_cast<EventBus*>(param)->eventBusTask();
}

void EventBus::eventBusTask() {
    GW_LOG_I(TAG, "Task running on core %d",
             static_cast<int>(xPortGetCoreID()));

    Event event;

    while (true) {
        if (xQueueReceive(m_queue, &event,
                          pdMS_TO_TICKS(1000)) == pdTRUE) {
            dispatchEvent(event);
        }
    }
}

// ============================================================
// getStats
// ============================================================
EventBus::Stats EventBus::getStats() const noexcept {
    Stats copy{};
    if (xSemaphoreTake(m_statsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = m_stats;
        xSemaphoreGive(m_statsMutex);
    }
    return copy;
}

// ============================================================
// getState
// ============================================================
ServiceState EventBus::getState() const {
    return m_state;
}

} // namespace Services
} // namespace Gateway