// ============================================================
// WebSocketHandler.h
// Real-time WebSocket communication handler.
//
// Design decisions:
//   - Auth required: WS connection validated via session cookie
//   - JSON protocol: all messages are JSON objects
//   - Message types: subscribe/unsubscribe/ping/event
//   - Server→Client: system events pushed to subscribed clients
//   - Max clients: enforced at connection time
//   - Ping/pong: keep-alive with 30s interval
//   - Per-client subscription filter (event types)
//   - Thread-safe: ESPAsyncWebServer handles its own threading
// ============================================================

#pragma once

#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include "../../core/ErrorCodes.h"
#include "../../core/SystemConfig.h"
#include "../../models/Session.h"
#include "../../interfaces/IEventListener.h"

#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdint>

namespace Gateway {
namespace Web {

class WebSocketHandler final
    : public Interfaces::IEventListener
{
public:
    static constexpr uint8_t MAX_WS_CLIENTS = 4;

    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static WebSocketHandler& getInstance() noexcept;

    // --------------------------------------------------------
    // Attach to AsyncWebSocket
    // --------------------------------------------------------
    [[nodiscard]] Result initialize(AsyncWebSocket* ws);

    // --------------------------------------------------------
    // ESPAsyncWebServer event callback
    // --------------------------------------------------------
    void onEvent(AsyncWebSocket*       server,
                  AsyncWebSocketClient* client,
                  AwsEventType          type,
                  void*                 arg,
                  uint8_t*              data,
                  size_t                len);

    // --------------------------------------------------------
    // IEventListener — receives system events and pushes to WS
    // --------------------------------------------------------
    void onEvent(const Interfaces::Event& event) override;

    [[nodiscard]] const Interfaces::EventType*
    getSubscribedEvents(size_t& outCount) const override;

    [[nodiscard]] const char*
    getListenerName() const override { return "WebSocketHandler"; }

    // --------------------------------------------------------
    // Broadcast to all authenticated clients
    // --------------------------------------------------------
    void broadcast(const char* jsonMessage);
    void broadcastEvent(const char* eventType, const char* payload);

    // --------------------------------------------------------
    // Send to specific client
    // --------------------------------------------------------
    void sendToClient(uint32_t clientId, const char* jsonMessage);

    // --------------------------------------------------------
    // Stats
    // --------------------------------------------------------
    [[nodiscard]] uint8_t getConnectedCount() const noexcept;

private:
    WebSocketHandler();
    ~WebSocketHandler() = default;

    WebSocketHandler(const WebSocketHandler&)            = delete;
    WebSocketHandler& operator=(const WebSocketHandler&) = delete;

    // --------------------------------------------------------
    // Client record
    // --------------------------------------------------------
    struct WSClient {
        uint32_t         id;
        bool             authenticated;
        Models::Session  session;
        uint32_t         lastPingMs;
        bool             active;

        WSClient()
            : id(0)
            , authenticated(false)
            , session{}
            , lastPingMs(0)
            , active(false)
        {}
    };

    // --------------------------------------------------------
    // Internal handlers
    // --------------------------------------------------------
    void handleConnect(AsyncWebSocketClient* client);
    void handleDisconnect(AsyncWebSocketClient* client);
    void handleMessage(AsyncWebSocketClient* client,
                        const uint8_t*        data,
                        size_t                len);
    void handlePong(AsyncWebSocketClient* client);

    // --------------------------------------------------------
    // Auth on connect
    // --------------------------------------------------------
    bool authenticateClient(AsyncWebSocketClient* client,
                             WSClient&             record);

    // --------------------------------------------------------
    // Message processing
    // --------------------------------------------------------
    void processSubscribe(WSClient&             client,
                           const char*           eventType);
    void processPing(AsyncWebSocketClient* client,
                      WSClient&             record);

    // --------------------------------------------------------
    // Find client record
    // --------------------------------------------------------
    WSClient* findClient(uint32_t id);

    // --------------------------------------------------------
    // Ping loop (called from tick)
    // --------------------------------------------------------
    void pingClients();

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    AsyncWebSocket*   m_ws;
    WSClient          m_clients[MAX_WS_CLIENTS];
    SemaphoreHandle_t m_mutex;

    static const Interfaces::EventType s_subscribedEvents[];
    static const size_t                s_subscribedEventCount;
};

} // namespace Web
} // namespace Gateway

#endif // WEBSOCKET_HANDLER_H