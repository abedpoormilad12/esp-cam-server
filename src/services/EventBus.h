// ============================================================
// EventBus.h
// Asynchronous publish/subscribe event system.
//
// Design decisions:
//   - Fixed-size subscriber table: zero heap allocation
//   - FreeRTOS queue for cross-task event delivery
//   - Static queue + task storage: predictable RAM
//   - Listener registration is mutex-protected
//   - Event dispatch is non-blocking (post to queue)
//   - Dedicated task processes and fans out events
//   - Supports per-event-type subscriptions
//   - Thread-safe publish from any task or ISR
// ============================================================

#pragma once

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "../interfaces/IService.h"
#include "../interfaces/IEventListener.h"
#include "../interfaces/IHealthCheck.h"
#include "../core/SystemConfig.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <cstdint>
#include <atomic>

namespace Gateway {
namespace Services {

using Interfaces::Event;
using Interfaces::EventType;
using Interfaces::IEventListener;

// ============================================================
// EventBus
// Central publish/subscribe hub for the entire firmware.
// ============================================================
class EventBus final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    // --------------------------------------------------------
    // Singleton access
    // --------------------------------------------------------
    [[nodiscard]] static EventBus& getInstance() noexcept;

    // --------------------------------------------------------
    // IService implementation
    // --------------------------------------------------------
    [[nodiscard]] Result        initialize()       override;
    [[nodiscard]] Result        start()            override;
    [[nodiscard]] Result        stop()             override;
    [[nodiscard]] Interfaces::ServiceState  getState()  const  override;
    [[nodiscard]] const char*   getName()   const  override { return "EventBus"; }
    [[nodiscard]] bool          isHealthy() const  override;

    // --------------------------------------------------------
    // IHealthCheck implementation
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "EventBus"; }

    // --------------------------------------------------------
    // Subscription management
    // A listener subscribes to specific event types.
    // Pass nullptr for subscribedEvents + 0 for count
    // to receive ALL events.
    // --------------------------------------------------------
    [[nodiscard]] Result subscribe(IEventListener* listener);
    [[nodiscard]] Result unsubscribe(IEventListener* listener);

    // --------------------------------------------------------
    // Event publishing
    // Non-blocking: posts to internal queue.
    // Safe to call from any task or ISR context.
    // --------------------------------------------------------
    Result publish(const Event& event);
    Result publish(EventType type, const char* message = nullptr);

    // Convenience: publish with uint32_t payload
    Result publish(EventType type,
                   uint32_t  data0,
                   uint32_t  data1   = 0,
                   uint32_t  data2   = 0,
                   uint32_t  data3   = 0);

    // --------------------------------------------------------
    // Synchronous publish (bypasses queue — use carefully)
    // Only safe to call from the EventBus task context or
    // when you are certain no deadlock can occur.
    // --------------------------------------------------------
    void publishSync(const Event& event);

    // --------------------------------------------------------
    // Statistics
    // --------------------------------------------------------
    struct Stats {
        uint32_t totalPublished;
        uint32_t totalDropped;
        uint32_t totalDispatched;
        uint32_t queueHighWaterMark;
        uint8_t  subscriberCount;
    };
    [[nodiscard]] Stats getStats() const noexcept;

private:
    // --------------------------------------------------------
    // Private constructor — singleton
    // --------------------------------------------------------
    EventBus();
    ~EventBus();

    EventBus(const EventBus&)            = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&)                 = delete;
    EventBus& operator=(EventBus&&)      = delete;

    // --------------------------------------------------------
    // Internal dispatch — called from task
    // --------------------------------------------------------
    void dispatchEvent(const Event& event);
    bool shouldDeliver(IEventListener* listener,
                       const Event&    event) const;

    // --------------------------------------------------------
    // FreeRTOS task
    // --------------------------------------------------------
    static void eventBusTaskEntry(void* param);
    void        eventBusTask();

    // --------------------------------------------------------
    // Constants from config
    // --------------------------------------------------------
    static constexpr uint8_t  QUEUE_LENGTH    = Config::EventBus::QUEUE_SIZE;
    static constexpr uint8_t  MAX_SUBSCRIBERS = Config::EventBus::MAX_LISTENERS_PER_EVENT
                                                * Config::EventBus::MAX_EVENT_TYPES / 4;
    static constexpr uint32_t TASK_STACK_WORDS =
        Config::Tasks::EVENT_BUS_STACK_WORDS;

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState            m_state;

    // Subscriber table — statically allocated
    IEventListener*         m_subscribers[32];
    uint8_t                 m_subscriberCount;
    SemaphoreHandle_t       m_subscriberMutex;

    // FreeRTOS queue — statically allocated
    QueueHandle_t           m_queue;
    StaticQueue_t           m_queueBuffer;
    uint8_t                 m_queueStorage[QUEUE_LENGTH * sizeof(Event)];

    // FreeRTOS task — statically allocated
    TaskHandle_t            m_taskHandle;
    StaticTask_t            m_taskTCB;
    StackType_t             m_taskStack[TASK_STACK_WORDS];

    // Statistics
    mutable SemaphoreHandle_t m_statsMutex;
    Stats                     m_stats;
};

} // namespace Services
} // namespace Gateway

#endif // EVENT_BUS_H