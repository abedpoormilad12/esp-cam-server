// ============================================================
// WebServer.h
// Production-grade async web server.
//
// Design decisions:
//   - ESPAsyncWebServer: non-blocking, event-driven
//   - Route registration: typed, explicit
//   - Middleware chain: applied per request category
//   - Static files: served from LittleFS /www/
//   - Body capture: AsyncBodyHandler for POST/PUT
//   - WebSocket: attached at /ws
//   - Security headers: every response
//   - ETag/Cache: for static assets (1 day)
//   - gzip: served if .gz file exists alongside original
//   - 404/405 handlers: proper error responses
// ============================================================

#pragma once

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "../interfaces/IService.h"
#include "../interfaces/IHealthCheck.h"
#include "HttpContext.h"
#include "HttpResponse.h"
#include "middleware/AuthMiddleware.h"
#include "middleware/CSRFMiddleware.h"
#include "middleware/RateLimitMiddleware.h"
#include "middleware/CORSMiddleware.h"
#include "middleware/LoggingMiddleware.h"
#include "handlers/AuthHandler.h"
#include "handlers/SystemHandler.h"
#include "handlers/WebSocketHandler.h"
#include "../core/SystemConfig.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <cstdint>

namespace Gateway {
namespace Web {

class WebServer final
    : public Interfaces::IService
    , public Interfaces::IHealthCheck
{
public:
    // --------------------------------------------------------
    // Singleton
    // --------------------------------------------------------
    [[nodiscard]] static WebServer& getInstance() noexcept;

    // --------------------------------------------------------
    // IService
    // --------------------------------------------------------
    [[nodiscard]] Result       initialize()       override;
    [[nodiscard]] Result       start()            override;
    [[nodiscard]] Result       stop()             override;
    [[nodiscard]] Interfaces::ServiceState getState()  const  override;
    [[nodiscard]] const char*  getName()   const  override { return "WebServer"; }
    [[nodiscard]] bool         isHealthy() const  override;

    // --------------------------------------------------------
    // IHealthCheck
    // --------------------------------------------------------
    [[nodiscard]] Interfaces::HealthReport getHealthReport() const override;
    [[nodiscard]] const char* getComponentName() const override { return "WebServer"; }

    // --------------------------------------------------------
    // WebSocket access for broadcasting
    // --------------------------------------------------------
    [[nodiscard]] WebSocketHandler& getWebSocket() noexcept;

private:
    WebServer();
    ~WebServer() = default;

    WebServer(const WebServer&)            = delete;
    WebServer& operator=(const WebServer&) = delete;

    // --------------------------------------------------------
    // Route registration
    // --------------------------------------------------------
    void registerStaticRoutes();
    void registerAuthRoutes();
    void registerSystemRoutes();
    void registerApiRoutes();
    void registerWebSocketRoute();
    void registerErrorHandlers();

    // --------------------------------------------------------
    // Middleware chain runner
    // Returns true if chain completed (request not handled)
    // --------------------------------------------------------
    bool runMiddlewareChain(
        HttpContext&  ctx,
        IMiddleware** chain,
        uint8_t       chainLen
    );

    // --------------------------------------------------------
    // Body capture helper
    // Collects POST/PUT body before handler dispatch
    // --------------------------------------------------------
    static bool validateBody(AsyncWebServerRequest* req,
                              size_t                 maxSize);

    // --------------------------------------------------------
    // Member data
    // --------------------------------------------------------
    Interfaces::ServiceState         m_state;
    AsyncWebServer       m_server;
    AsyncWebSocket       m_wsSocket;

    // Middleware instances (stateless — shared across requests)
    LoggingMiddleware    m_loggingMw;
    CORSMiddleware       m_corsMw;
    RateLimitMiddleware  m_rateLimitMw;
    AuthMiddleware       m_authRequiredMw;
    AuthMiddleware       m_authOptionalMw;
    CSRFMiddleware       m_csrfMw;
};

} // namespace Web
} // namespace Gateway

#endif // WEB_SERVER_H