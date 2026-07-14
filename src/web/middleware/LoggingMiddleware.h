// ============================================================
// LoggingMiddleware.h
// Access logging for all HTTP requests.
// ============================================================

#pragma once

#ifndef LOGGING_MIDDLEWARE_H
#define LOGGING_MIDDLEWARE_H

#include "IMiddleware.h"

namespace Gateway {
namespace Web {

class LoggingMiddleware final : public IMiddleware {
public:
    LoggingMiddleware() = default;

    [[nodiscard]] bool        process(HttpContext& ctx) override;
    [[nodiscard]] const char* getName() const override {
        return "LoggingMiddleware";
    }
};

} // namespace Web
} // namespace Gateway

#endif // LOGGING_MIDDLEWARE_H