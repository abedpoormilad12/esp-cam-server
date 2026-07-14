// ============================================================
// WebSocketHandler.cpp
// ============================================================

#include "WebSocketHandler.h"
#include "../../auth/AuthManager.h"
#include "../../auth/SessionManager.h"
#include "../../services/Logger.h"
#include "../../services/EventBus.h"
#include "../../core/SystemConfig.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "WebSocket";

// ============================================================
// Subscribed system events to forward to WS clients
// ============================================================
const Interfaces::EventType WebSocketHandler::s_subscribedEvents[] = {
    Interfaces::EventType::SYSTEM_STATE_CHANGED,
    Interfaces::EventType::SYSTEM_BOOT_COMPLETE,
    Interfaces::EventType::NETWORK_WIFI_CONNECTED,
    Interfaces::EventType::NETWORK_WIFI_DISCONNECTED,
    Interfaces::EventType::NETWORK_WIFI_GOT_IP,
    Interfaces::EventType::HEALTH_WARNING,
    Interfaces::EventType::HEALTH_CRITICAL,
    Interfaces::EventType::HEALTH_RECOVERED,
    Interfaces::EventType::USER_CREATED,
    Interfaces::EventType::USER_DELETED,
    Interfaces::EventType::AUTH_LOGIN_SUCCESS,
    Interfaces::EventType::AUTH_LOGOUT,
    Interfaces::EventType::DEVICE_REGISTERED,
    Interfaces::EventType::DEVICE_OFFLINE,
};

const size_t WebSocketHandler::s_subscribedEventCount =
    sizeof(s_subscribedEvents) / sizeof(s_subscribedEvents[0]);

// ============================================================
// Singleton
// ============================================================
WebSocketHandler& WebSocketHandler::getInstance() noexcept {
    static WebSocketHandler instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
WebSocketHandler::WebSocketHandler()
    : m_ws(nullptr)
    , m_clients{}
    , m_mutex(nullptr)
{
}

// ============================================================
// initialize
// ============================================================
Result WebSocketHandler::initialize(AsyncWebSocket* ws) {
    if (!ws) return Result::ERR_NULL_POINTER;

    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) return Result::ERR_OUT_OF_MEMORY;

    m_ws = ws;

    // Subscribe to EventBus
    Services::EventBus::getInstance().subscribe(this);

    GW_LOG_I(TAG, "Initialized. Max clients: %d", MAX_WS_CLIENTS);
    return Result::OK;
}

// ============================================================
// ESPAsyncWebServer event dispatch
// ============================================================
void WebSocketHandler::onEvent(AsyncWebSocket*       server,
                                AsyncWebSocketClient* client,
                                AwsEventType          type,
                                void*                 arg,
                                uint8_t*              data,
                                size_t                len) {
    switch (type) {
        case WS_EVT_CONNECT:
            handleConnect(client);
            break;

        case WS_EVT_DISCONNECT:
            handleDisconnect(client);
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
            if (info->final && info->index == 0 &&
                info->len == len &&
                info->opcode == WS_TEXT) {
                handleMessage(client, data, len);
            }
            break;
        }

        case WS_EVT_PONG:
            handlePong(client);
            break;

        case WS_EVT_ERROR:
            GW_LOG_W(TAG, "WS error on client %lu",
                     static_cast<unsigned long>(client->id()));
            break;

        default:
            break;
    }
}

// ============================================================
// handleConnect
// ============================================================
void WebSocketHandler::handleConnect(AsyncWebSocketClient* client) {
    if (!client) return;

    GW_LOG_D(TAG, "Client connecting: id=%lu ip=%s",
             static_cast<unsigned long>(client->id()),
             client->remoteIP().toString().c_str());

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        client->close(1013, "Server busy");
        return;
    }

    // Check capacity
    WSClient* slot = nullptr;
    uint8_t   activeCount = 0;

    for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (m_clients[i].active) {
            activeCount++;
        } else if (!slot) {
            slot = &m_clients[i];
        }
    }

    if (!slot || activeCount >= MAX_WS_CLIENTS) {
        xSemaphoreGive(m_mutex);
        client->close(1013, "Max clients reached");
        GW_LOG_W(TAG, "Connection rejected: max clients (%d)",
                 MAX_WS_CLIENTS);
        return;
    }

    // Initialize slot
    *slot = WSClient{};
    slot->id         = client->id();
    slot->lastPingMs = static_cast<uint32_t>(millis());
    slot->active     = true;

    // Authenticate via session cookie
    bool authed = authenticateClient(client, *slot);

    xSemaphoreGive(m_mutex);

    if (!authed) {
        // Send auth required message
        client->text(
            "{\"type\":\"auth_required\","
            "\"message\":\"Send session cookie to authenticate\"}"
        );
    } else {
        // Send welcome
        char welcome[256];
        snprintf(welcome, sizeof(welcome),
                 "{\"type\":\"connected\","
                 "\"message\":\"Welcome %s\","
                 "\"role\":\"%s\","
                 "\"serverTime\":%lu}",
                 slot->session.username,
                 Models::roleToString(slot->session.role),
                 static_cast<unsigned long>(millis() / 1000));

        client->text(welcome);
        GW_LOG_I(TAG, "WS client authenticated: user='%s' id=%lu",
                 slot->session.username,
                 static_cast<unsigned long>(client->id()));
    }
}

// ============================================================
// authenticateClient
// ============================================================
bool WebSocketHandler::authenticateClient(
    AsyncWebSocketClient* client,
    WSClient&             record)
{
    // Extract session cookie from handshake request
    // In ESPAsyncWebServer, cookie access during WS upgrade
    // must be checked in the handshake handler (onFilter).
    // For now, we mark as unauthenticated until client
    // sends a token message.
    record.authenticated = false;
    return false;
}

// ============================================================
// handleDisconnect
// ============================================================
void WebSocketHandler::handleDisconnect(
    AsyncWebSocketClient* client) {
    if (!client) return;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        WSClient* rec = findClient(client->id());
        if (rec) {
            GW_LOG_D(TAG, "WS client disconnected: id=%lu user='%s'",
                     static_cast<unsigned long>(client->id()),
                     rec->authenticated ? rec->session.username : "anon");
            rec->active = false;
        }
        xSemaphoreGive(m_mutex);
    }
}

// ============================================================
// handleMessage
// ============================================================
void WebSocketHandler::handleMessage(AsyncWebSocketClient* client,
                                      const uint8_t*        data,
                                      size_t                len) {
    if (!client || !data || len == 0) return;
    if (len > 512) {
        client->text("{\"type\":\"error\",\"message\":\"Message too large\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc,
        reinterpret_cast<const char*>(data),
        len
    );

    if (err) {
        client->text("{\"type\":\"error\",\"message\":\"Invalid JSON\"}");
        return;
    }

    const char* type = doc["type"] | "";

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    WSClient* rec = findClient(client->id());

    if (!rec) {
        xSemaphoreGive(m_mutex);
        return;
    }

    // Authenticate message: client sends session token
    if (strcmp(type, "auth") == 0) {
        const char* sessionId = doc["sessionId"] | "";

        if (sessionId[0] != '\0') {
            Models::Session session;
            IPAddress ip  = client->remoteIP();
            char ipStr[16];
            snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
                     ip[0], ip[1], ip[2], ip[3]);

            Result r = Auth::SessionManager::getInstance()
                           .validateSession(sessionId, ipStr, session);

            if (GW_OK(r)) {
                rec->authenticated = true;
                rec->session       = session;

                xSemaphoreGive(m_mutex);

                char resp[128];
                snprintf(resp, sizeof(resp),
                         "{\"type\":\"authenticated\","
                         "\"user\":\"%s\","
                         "\"role\":\"%s\"}",
                         session.username,
                         Models::roleToString(session.role));
                client->text(resp);
                return;
            }
        }

        xSemaphoreGive(m_mutex);
        client->text("{\"type\":\"auth_failed\","
                     "\"message\":\"Invalid session\"}");
        return;
    }

    // Ping
    if (strcmp(type, "ping") == 0) {
        processPing(client, *rec);
        xSemaphoreGive(m_mutex);
        return;
    }

    // All other messages require authentication
    if (!rec->authenticated) {
        xSemaphoreGive(m_mutex);
        client->text("{\"type\":\"error\","
                     "\"message\":\"Authentication required\"}");
        return;
    }

    // Subscribe to event type
    if (strcmp(type, "subscribe") == 0) {
        const char* eventType = doc["event"] | "";
        processSubscribe(*rec, eventType);
        xSemaphoreGive(m_mutex);
        client->text("{\"type\":\"subscribed\"}");
        return;
    }

    xSemaphoreGive(m_mutex);
    client->text("{\"type\":\"error\",\"message\":\"Unknown type\"}");
}

// ============================================================
// processPing
// ============================================================
void WebSocketHandler::processPing(AsyncWebSocketClient* client,
                                    WSClient&             record) {
    record.lastPingMs = static_cast<uint32_t>(millis());
    client->text("{\"type\":\"pong\",\"ts\":" +
                  String(millis()) + "}");
}

// ============================================================
// processSubscribe
// ============================================================
void WebSocketHandler::processSubscribe(WSClient&   client,
                                         const char* eventType) {
    // For now: acknowledge subscription
    // Full event filtering per client can be added here
    GW_LOG_D(TAG, "Client %lu subscribed to: %s",
             static_cast<unsigned long>(client.id), eventType);
}

// ============================================================
// handlePong
// ============================================================
void WebSocketHandler::handlePong(AsyncWebSocketClient* client) {
    if (!client) return;

    if (xSemaphoreTake(m_mutex, 0) == pdTRUE) {
        WSClient* rec = findClient(client->id());
        if (rec) {
            rec->lastPingMs = static_cast<uint32_t>(millis());
        }
        xSemaphoreGive(m_mutex);
    }
}

// ============================================================
// IEventListener::onEvent — forward system events to WS
// ============================================================
void WebSocketHandler::onEvent(const Interfaces::Event& event) {
    if (!m_ws) return;

    const char* eventName = Interfaces::systemEventToString(
        static_cast<Core::SystemEvent>(0xFF)
    );

    // Map EventType to string
    char eventType[32] = "system.event";

    switch (event.type) {
        case Interfaces::EventType::SYSTEM_STATE_CHANGED:
            strncpy(eventType, "system.stateChanged",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::NETWORK_WIFI_GOT_IP:
            strncpy(eventType, "network.gotIP",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::NETWORK_WIFI_DISCONNECTED:
            strncpy(eventType, "network.disconnected",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::HEALTH_WARNING:
            strncpy(eventType, "health.warning",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::HEALTH_CRITICAL:
            strncpy(eventType, "health.critical",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::AUTH_LOGIN_SUCCESS:
            strncpy(eventType, "auth.login",
                    sizeof(eventType) - 1);
            break;
        case Interfaces::EventType::AUTH_LOGOUT:
            strncpy(eventType, "auth.logout",
                    sizeof(eventType) - 1);
            break;
        default:
            break;
    }

    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"type\":\"event\","
             "\"event\":\"%s\","
             "\"data\":[%lu,%lu,%lu,%lu],"
             "\"ts\":%lu}",
             eventType,
             static_cast<unsigned long>(event.data[0]),
             static_cast<unsigned long>(event.data[1]),
             static_cast<unsigned long>(event.data[2]),
             static_cast<unsigned long>(event.data[3]),
             static_cast<unsigned long>(event.timestamp));

    broadcast(payload);
}

// ============================================================
// getSubscribedEvents
// ============================================================
const Interfaces::EventType*
WebSocketHandler::getSubscribedEvents(size_t& outCount) const {
    outCount = s_subscribedEventCount;
    return s_subscribedEvents;
}

// ============================================================
// broadcast
// ============================================================
void WebSocketHandler::broadcast(const char* jsonMessage) {
    if (!m_ws || !jsonMessage) return;
    m_ws->textAll(jsonMessage);
}

// ============================================================
// broadcastEvent
// ============================================================
void WebSocketHandler::broadcastEvent(const char* eventType,
                                       const char* payload) {
    if (!eventType) return;

    char msg[384];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"event\",\"event\":\"%s\","
             "\"payload\":%s,\"ts\":%lu}",
             eventType,
             payload ? payload : "{}",
             static_cast<unsigned long>(millis()));

    broadcast(msg);
}

// ============================================================
// sendToClient
// ============================================================
void WebSocketHandler::sendToClient(uint32_t    clientId,
                                     const char* jsonMessage) {
    if (!m_ws || !jsonMessage) return;

    AsyncWebSocketClient* client = m_ws->client(clientId);
    if (client && client->status() == WS_CONNECTED) {
        client->text(jsonMessage);
    }
}

// ============================================================
// getConnectedCount
// ============================================================
uint8_t WebSocketHandler::getConnectedCount() const noexcept {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (m_clients[i].active) count++;
    }
    return count;
}

// ============================================================
// findClient
// ============================================================
WebSocketHandler::WSClient*
WebSocketHandler::findClient(uint32_t id) {
    for (uint8_t i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (m_clients[i].active && m_clients[i].id == id) {
            return &m_clients[i];
        }
    }
    return nullptr;
}

} // namespace Web
} // namespace Gateway