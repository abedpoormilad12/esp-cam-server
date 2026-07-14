// ============================================================
// IEventListener.h
// Interface for all event listeners in the EventBus system.
// ============================================================

#pragma once

#ifndef IEVENT_LISTENER_H
#define IEVENT_LISTENER_H

#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Interfaces {

// ============================================================
// Event types - centralized enum for the whole system
// ============================================================
enum class EventType : uint16_t {
    // System events
    SYSTEM_BOOT_COMPLETE        = 0x0001,
    SYSTEM_SHUTDOWN_REQUESTED   = 0x0002,
    SYSTEM_ERROR                = 0x0003,
    SYSTEM_WATCHDOG_RESET       = 0x0004,
    SYSTEM_STATE_CHANGED        = 0x0005,

    // Network events
    NETWORK_WIFI_CONNECTED      = 0x0100,
    NETWORK_WIFI_DISCONNECTED   = 0x0101,
    NETWORK_WIFI_GOT_IP         = 0x0102,
    NETWORK_AP_CLIENT_CONNECTED = 0x0103,
    NETWORK_AP_CLIENT_DISCONNECTED = 0x0104,

    // Auth events
    AUTH_LOGIN_SUCCESS          = 0x0200,
    AUTH_LOGIN_FAILED           = 0x0201,
    AUTH_LOGOUT                 = 0x0202,
    AUTH_SESSION_EXPIRED        = 0x0203,
    AUTH_ACCOUNT_LOCKED         = 0x0204,

    // User events
    USER_CREATED                = 0x0300,
    USER_DELETED                = 0x0301,
    USER_UPDATED                = 0x0302,
    USER_PASSWORD_CHANGED       = 0x0303,

    // Storage events
    STORAGE_MOUNTED             = 0x0400,
    STORAGE_UNMOUNTED           = 0x0401,
    STORAGE_ERROR               = 0x0402,
    STORAGE_LOW_SPACE           = 0x0403,

    // Health events
    HEALTH_WARNING              = 0x0500,
    HEALTH_CRITICAL             = 0x0501,
    HEALTH_RECOVERED            = 0x0502,
    HEALTH_HEAP_LOW             = 0x0503,

    // Device events (future)
    DEVICE_REGISTERED           = 0x0600,
    DEVICE_UNREGISTERED         = 0x0601,
    DEVICE_ONLINE               = 0x0602,
    DEVICE_OFFLINE              = 0x0603,
    DEVICE_DATA_RECEIVED        = 0x0604,

    // OTA events (future)
    OTA_STARTED                 = 0x0700,
    OTA_PROGRESS                = 0x0701,
    OTA_COMPLETE                = 0x0702,
    OTA_FAILED                  = 0x0703,

    // Sentinel
    INVALID                     = 0xFFFF
};

// ============================================================
// Event payload - fixed-size to avoid heap allocation
// ============================================================
struct Event {
    EventType   type;
    uint32_t    timestamp;      // millis() at creation
    uint32_t    data[4];        // 16 bytes of raw data
    char        message[32];    // optional string message

    Event() : type(EventType::INVALID), timestamp(0), data{}, message{} {}

    explicit Event(EventType t, uint32_t ts = 0)
        : type(t), timestamp(ts), data{}, message{} {}
};

static_assert(sizeof(Event) <= 64, "Event struct exceeds size limit");

// ============================================================
// IEventListener
// ============================================================
class IEventListener {
public:
    virtual ~IEventListener() = default;

    // Called by EventBus when an event is dispatched
    // NOTE: May be called from a different task context.
    //       Implementations must be thread-safe.
    virtual void onEvent(const Event& event) = 0;

    // Which events this listener wants to receive
    // Return nullptr for "all events"
    [[nodiscard]] virtual const EventType* getSubscribedEvents(
        size_t& outCount
    ) const = 0;

    [[nodiscard]] virtual const char* getListenerName() const = 0;

protected:
    IEventListener() = default;

    IEventListener(const IEventListener&)            = delete;
    IEventListener& operator=(const IEventListener&) = delete;
};

} // namespace Interfaces
} // namespace Gateway

#endif // IEVENT_LISTENER_H