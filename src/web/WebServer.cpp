// ============================================================
// WebServer.cpp
// ============================================================

#include "WebServer.h"
#include "../services/Logger.h"
#include "../services/EventBus.h"
#include "../config/ConfigManager.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <cstdio>

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "WebServer";

// ============================================================
// Singleton
// ============================================================
WebServer& WebServer::getInstance() noexcept {
    static WebServer instance;
    return instance;
}

// ============================================================
// Constructor
// ============================================================
WebServer::WebServer()
    : m_state(ServiceState::UNINITIALIZED)
    , m_server(Config::Network::HTTP_PORT)
    , m_wsSocket("/ws")
    , m_loggingMw{}
    , m_corsMw{}
    , m_rateLimitMw(Security::RateLimitPolicy::general())
    , m_authRequiredMw(true)
    , m_authOptionalMw(false)
    , m_csrfMw{}
{
}

// ============================================================
// IService::initialize
// ============================================================
Result WebServer::initialize() {
    if (m_state != ServiceState::UNINITIALIZED) {
        return Result::ERR_ALREADY_INITIALIZED;
    }

    m_state = ServiceState::INITIALIZING;

    // Initialize WebSocket handler
    Result r = WebSocketHandler::getInstance()
                   .initialize(&m_wsSocket);
    if (GW_ERR(r)) {
        GW_LOG_E(TAG, "WebSocket init failed: %s",
                 ResultHelper::toString(r));
        m_state = ServiceState::FAULTED;
        return r;
    }

    m_state = ServiceState::STOPPED;
    GW_LOG_I(TAG, "Initialized.");
    return Result::OK;
}

// ============================================================
// IService::start
// ============================================================
Result WebServer::start() {
    if (m_state != ServiceState::STOPPED) {
        return Result::ERR_INVALID_STATE;
    }

    // Register all routes
    registerStaticRoutes();
    registerAuthRoutes();
    registerSystemRoutes();
    registerApiRoutes();
    registerWebSocketRoute();
    registerErrorHandlers();

    // Start the server
    m_server.begin();

    m_state = ServiceState::RUNNING;

    GW_LOG_I(TAG, "HTTP server started on port %d",
             static_cast<int>(Config::Network::HTTP_PORT));

    Services::EventBus::getInstance().publish(
        Interfaces::EventType::SYSTEM_BOOT_COMPLETE,
        "WebServer started"
    );

    return Result::OK;
}

// ============================================================
// IService::stop
// ============================================================
Result WebServer::stop() {
    if (m_state != ServiceState::RUNNING) {
        return Result::ERR_INVALID_STATE;
    }

    m_server.end();
    m_state = ServiceState::STOPPED;
    GW_LOG_I(TAG, "Stopped.");
    return Result::OK;
}

// ============================================================
// IService::isHealthy
// ============================================================
bool WebServer::isHealthy() const {
    return m_state == ServiceState::RUNNING;
}

// ============================================================
// IService::getState
// ============================================================
ServiceState WebServer::getState() const {
    return m_state;
}

// ============================================================
// IHealthCheck::getHealthReport
// ============================================================
Interfaces::HealthReport WebServer::getHealthReport() const {
    Interfaces::HealthReport report;
    report.componentName = getComponentName();
    report.lastCheckMs   = static_cast<uint32_t>(millis());

    report.status = isHealthy()
        ? Interfaces::HealthStatus::HEALTHY
        : Interfaces::HealthStatus::CRITICAL;

    snprintf(report.detail, sizeof(report.detail),
             "Port:%d WS-clients:%d State:%d",
             static_cast<int>(Config::Network::HTTP_PORT),
             static_cast<int>(
                 WebSocketHandler::getInstance().getConnectedCount()),
             static_cast<int>(m_state));

    return report;
}

// ============================================================
// getWebSocket
// ============================================================
WebSocketHandler& WebServer::getWebSocket() noexcept {
    return WebSocketHandler::getInstance();
}

// ============================================================
// runMiddlewareChain
// ============================================================
bool WebServer::runMiddlewareChain(
    HttpContext&  ctx,
    IMiddleware** chain,
    uint8_t       chainLen)
{
    for (uint8_t i = 0; i < chainLen; ++i) {
        if (!chain[i]->process(ctx)) {
            return false;  // Chain terminated
        }
        if (ctx.handled || ctx.aborted) {
            return false;
        }
    }
    return true;
}

// ============================================================
// registerStaticRoutes
// Serve files from LittleFS /www/ directory
// ============================================================
void WebServer::registerStaticRoutes() {
    // Root → index.html (login page)
    m_server.on("/", HTTP_GET,
        [](AsyncWebServerRequest* req) {
            req->redirect("/index.html");
        }
    );

    // Serve static files from LittleFS
    m_server.serveStatic("/", LittleFS, "/www/")
            .setDefaultFile("index.html")
            .setCacheControl("max-age=86400");

    GW_LOG_D(TAG, "Static routes registered.");
}

// ============================================================
// registerAuthRoutes
// ============================================================
void WebServer::registerAuthRoutes() {
    // POST /api/auth/login — public, rate limited
    m_server.addHandler(
        new AsyncCallbackWebHandler()
    );

    m_server.on("/api/auth/login", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            // Body handled via onRequestBody below
        },
        nullptr,  // upload handler
        [this](AsyncWebServerRequest* req,
               uint8_t*               data,
               size_t                 len,
               size_t                 index,
               size_t                 total) {
            // Only process when full body received
            if (index + len != total) return;

            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            AuthHandler::getInstance().handleLogin(
                ctx,
                reinterpret_cast<const char*>(data),
                len
            );
        }
    );

    // POST /api/auth/logout — requires auth
    m_server.on("/api/auth/logout", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw,
                &m_authRequiredMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            AuthHandler::getInstance().handleLogout(ctx);
        }
    );

    // GET /api/auth/me
    m_server.on("/api/auth/me", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw,
                &m_authRequiredMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            AuthHandler::getInstance().handleMe(ctx);
        }
    );

    // GET /api/auth/csrf
    m_server.on("/api/auth/csrf", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_authRequiredMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            AuthHandler::getInstance().handleGetCSRF(ctx);
        }
    );

    GW_LOG_D(TAG, "Auth routes registered.");
}

// ============================================================
// registerSystemRoutes
// ============================================================
void WebServer::registerSystemRoutes() {
    // GET /api/system/status
    m_server.on("/api/system/status", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw,
                &m_authRequiredMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            SystemHandler::getInstance().handleStatus(ctx);
        }
    );

    // GET /api/system/health
    m_server.on("/api/system/health", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw,
                &m_authRequiredMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            SystemHandler::getInstance().handleHealth(ctx);
        }
    );

    // POST /api/system/restart
    m_server.on("/api/system/restart", HTTP_POST,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw,
                &m_authRequiredMw,
                &m_csrfMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            SystemHandler::getInstance().handleRestart(ctx);
        }
    );

    // GET /api/system/info (public — no auth required)
    m_server.on("/api/system/info", HTTP_GET,
        [this](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            IMiddleware* chain[] = {
                &m_loggingMw,
                &m_corsMw,
                &m_rateLimitMw
            };

            if (!runMiddlewareChain(ctx, chain,
                    sizeof(chain) / sizeof(chain[0]))) {
                return;
            }

            SystemHandler::getInstance().handleInfo(ctx);
        }
    );

    GW_LOG_D(TAG, "System routes registered.");
}

// ============================================================
// registerApiRoutes
// Placeholder for future device/sensor/camera API routes
// ============================================================
void WebServer::registerApiRoutes() {
    // Future: /api/devices/*
    // Future: /api/sensors/*
    // Future: /api/cameras/*

    GW_LOG_D(TAG, "API routes registered (base).");
}

// ============================================================
// registerWebSocketRoute
// ============================================================
void WebServer::registerWebSocketRoute() {
    m_wsSocket.onEvent(
        [](AsyncWebSocket*       server,
           AsyncWebSocketClient* client,
           AwsEventType          type,
           void*                 arg,
           uint8_t*              data,
           size_t                len) {
            WebSocketHandler::getInstance().onEvent(
                server, client, type, arg, data, len
            );
        }
    );

    m_server.addHandler(&m_wsSocket);

    GW_LOG_D(TAG, "WebSocket route registered at /ws");
}

// ============================================================
// registerErrorHandlers
// ============================================================
void WebServer::registerErrorHandlers() {
    // 404 handler
    m_server.onNotFound(
        [](AsyncWebServerRequest* req) {
            HttpContext ctx(req);

            if (HttpResponse::isApiRequest(req)) {
                HttpResponse::sendError(
                    req,
                    HttpStatus::NOT_FOUND,
                    "NotFound",
                    req->url().c_str()
                );
            } else {
                // Redirect to login for browser requests
                req->redirect("/index.html");
            }
        }
    );

    GW_LOG_D(TAG, "Error handlers registered.");
}

} // namespace Web
} // namespace Gateway