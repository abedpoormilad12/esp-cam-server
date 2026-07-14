// ============================================================
// SystemHandler.h
// Handles /api/system/* endpoints.
// ============================================================

#pragma once

#ifndef SYSTEM_HANDLER_H
#define SYSTEM_HANDLER_H

#include "../HttpContext.h"

namespace Gateway {
namespace Web {

class SystemHandler final {
public:
    static SystemHandler& getInstance() noexcept;

    // GET  /api/system/status
    void handleStatus(HttpContext& ctx);

    // GET  /api/system/health
    void handleHealth(HttpContext& ctx);

    // POST /api/system/restart
    void handleRestart(HttpContext& ctx);

    // GET  /api/system/info
    void handleInfo(HttpContext& ctx);

private:
    SystemHandler()  = default;
    ~SystemHandler() = default;

    SystemHandler(const SystemHandler&)            = delete;
    SystemHandler& operator=(const SystemHandler&) = delete;
};

} // namespace Web
} // namespace Gateway

#endif // SYSTEM_HANDLER_H