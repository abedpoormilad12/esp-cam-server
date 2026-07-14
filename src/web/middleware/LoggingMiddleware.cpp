// ============================================================
// LoggingMiddleware.cpp
// ============================================================

#include "LoggingMiddleware.h"
#include "../../services/Logger.h"

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "HTTP";

bool LoggingMiddleware::process(HttpContext& ctx) {
    GW_LOG_D(TAG, "%s %s from %s user='%s'",
             ctx.method,
             ctx.uri,
             ctx.clientIP,
             ctx.authenticated ? ctx.session.username : "anon");
    return true;
}

} // namespace Web
} // namespace Gateway