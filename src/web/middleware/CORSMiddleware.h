// ============================================================
// CORSMiddleware.h
// Cross-Origin Resource Sharing headers.
// ============================================================

#pragma once

#ifndef CORS_MIDDLEWARE_H
#define CORS_MIDDLEWARE_H

#include "IMiddleware.h"

namespace Gateway {
namespace Web {

class CORSMiddleware final : public IMiddleware {
public:
    CORSMiddleware() = default;

    [[nodiscard]] bool        process(HttpContext& ctx) override;
    [[nodiscard]] const char* getName() const override {
        return "CORSMiddleware";
    }
};

} // namespace Web
} // namespace Gateway

#endif // CORS_MIDDLEWARE_H