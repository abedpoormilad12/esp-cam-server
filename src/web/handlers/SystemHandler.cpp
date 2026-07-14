// ============================================================
// SystemHandler.cpp
// ============================================================

#include "SystemHandler.h"
#include "../HttpResponse.h"
#include "../../core/StateMachine.h"
#include "../../network/NetworkManager.h"
#include "../../auth/SessionManager.h"
#include "../../services/Logger.h"

#include <Arduino.h>
#include <esp_system.h>
#include <cstdio>

namespace Gateway {
namespace Web {

static constexpr const char* TAG = "SystemHandler";

SystemHandler& SystemHandler::getInstance() noexcept {
    static SystemHandler instance;
    return instance;
}

// ============================================================
// GET /api/system/status
// ============================================================
void SystemHandler::handleStatus(HttpContext& ctx) {
    if (!ctx.hasRole(Models::UserRole::VIEWER)) {
        HttpResponse::sendForbidden(ctx.request);
        return;
    }

    auto& sm      = Core::StateMachine::getInstance();
    auto& netMgr  = Network::NetworkManager::getInstance();

    Network::NetworkInfo netInfo = netMgr.getNetworkInfo();
    auto sessionStats = Auth::SessionManager::getInstance().getStats();

    char body[768];
    snprintf(body, sizeof(body),
             "{"
             "\"success\":true,"
             "\"system\":{"
             "\"state\":\"%s\","
             "\"uptime\":%lu,"
             "\"freeHeap\":%lu,"
             "\"minFreeHeap\":%lu,"
             "\"cpuFreqMHz\":%d,"
             "\"resetReason\":%d"
             "},"
             "\"network\":{"
             "\"connected\":%s,"
             "\"ip\":\"%s\","
             "\"rssi\":%d,"
             "\"hostname\":\"%s\","
             "\"apActive\":%s"
             "},"
             "\"sessions\":{"
             "\"active\":%d,"
             "\"max\":%d"
             "}"
             "}",
             Core::systemStateToString(sm.getCurrentState()),
             static_cast<unsigned long>(millis() / 1000),
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(ESP.getMinFreeHeap()),
             static_cast<int>(ESP.getCpuFreqMHz()),
             static_cast<int>(esp_reset_reason()),
             netInfo.staConnected ? "true" : "false",
             netInfo.staIP,
             static_cast<int>(netInfo.staRSSI),
             netInfo.hostname,
             netInfo.apActive ? "true" : "false",
             static_cast<int>(sessionStats.currentActive),
             static_cast<int>(Auth::SessionManager::MAX_SESSIONS));

    HttpResponse::sendJSON(ctx.request, HttpStatus::OK, body);
}

// ============================================================
// GET /api/system/health
// ============================================================
void SystemHandler::handleHealth(HttpContext& ctx) {
    if (!ctx.hasRole(Models::UserRole::ADMIN)) {
        HttpResponse::sendForbidden(ctx.request);
        return;
    }

    bool healthy = Core::StateMachine::getInstance().isOperational();

    char body[256];
    snprintf(body, sizeof(body),
             "{\"success\":true,"
             "\"healthy\":%s,"
             "\"state\":\"%s\","
             "\"heap\":%lu,"
             "\"uptime\":%lu}",
             healthy ? "true" : "false",
             Core::systemStateToString(
                 Core::StateMachine::getInstance().getCurrentState()),
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<unsigned long>(millis() / 1000));

    HttpResponse::sendJSON(ctx.request, HttpStatus::OK, body);
}

// ============================================================
// POST /api/system/restart
// ============================================================
void SystemHandler::handleRestart(HttpContext& ctx) {
    if (!ctx.hasPermission(Models::Permission::RESTART_SYSTEM)) {
        HttpResponse::sendForbidden(ctx.request);
        return;
    }

    GW_LOG_W(TAG, "Restart requested by user '%s' from %s",
             ctx.getUsername(), ctx.clientIP);

    HttpResponse::sendMessage(ctx.request,
                               HttpStatus::OK,
                               "Restarting...");

    // Schedule restart after response is sent
    Core::StateMachine::getInstance().postEvent(
        Core::SystemEvent::RESTART_REQUESTED
    );

    ctx.handled = true;
}

// ============================================================
// GET /api/system/info
// ============================================================
void SystemHandler::handleInfo(HttpContext& ctx) {
    char body[512];
    snprintf(body, sizeof(body),
             "{\"success\":true,"
             "\"firmware\":{"
             "\"project\":\"%s\","
             "\"version\":\"%s\","
             "\"build\":\"%s\","
             "\"date\":\"%s\""
             "},"
             "\"hardware\":{"
             "\"chip\":\"ESP32\","
             "\"cores\":%d,"
             "\"flashSize\":%lu,"
             "\"psram\":%s"
             "}}",
             Config::FirmwareInfo::PROJECT,
             Config::FirmwareInfo::VERSION_STR,
             Config::FirmwareInfo::BUILD_TYPE,
             __DATE__,
             static_cast<int>(ESP.getChipCores()),
             static_cast<unsigned long>(ESP.getFlashChipSize()),
             ESP.getPsramSize() > 0 ? "true" : "false");

    HttpResponse::sendJSON(ctx.request, HttpStatus::OK, body);
}

} // namespace Web
} // namespace Gateway