// ============================================================
// CORSMiddleware.cpp
// ============================================================

#include "CORSMiddleware.h"
#include "../HttpResponse.h"

namespace Gateway {
namespace Web {

bool CORSMiddleware::process(HttpContext& ctx) {
    // Handle OPTIONS preflight
    if (strncmp(ctx.method, "OPTIONS", 7) == 0) {
        AsyncWebServerResponse* resp =
            ctx.request->beginResponse(204);

        resp->addHeader("Access-Control-Allow-Origin",  "*");
        resp->addHeader("Access-Control-Allow-Methods",
                        "GET, POST, PUT, DELETE, OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers",
                        "Content-Type, X-CSRF-Token, Authorization");
        resp->addHeader("Access-Control-Max-Age", "86400");
        HttpResponse::addSecurityHeaders(resp);

        ctx.request->send(resp);
        ctx.handled = true;
        return false;
    }

    return true;
}

} // namespace Web
} // namespace Gateway