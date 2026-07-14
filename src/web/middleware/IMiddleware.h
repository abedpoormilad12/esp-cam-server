// ============================================================
// IMiddleware.h
// Abstract middleware interface.
//
// Design decisions:
//   - Chain of Responsibility pattern
//   - Each middleware can: pass through, modify context,
//     or terminate chain (by setting ctx.handled = true)
//   - Middleware is stateless where possible
//   - Context carries all mutable request/response state
// ============================================================

#pragma once

#ifndef IMIDDLEWARE_H
#define IMIDDLEWARE_H

#include "../HttpContext.h"

namespace Gateway {
namespace Web {

class IMiddleware {
public:
    virtual ~IMiddleware() = default;

    // Process request. Return true to continue chain.
    // Return false or set ctx.handled to stop chain.
    [[nodiscard]] virtual bool process(HttpContext& ctx) = 0;

    [[nodiscard]] virtual const char* getName() const = 0;

protected:
    IMiddleware()                              = default;
    IMiddleware(const IMiddleware&)            = delete;
    IMiddleware& operator=(const IMiddleware&) = delete;
};

} // namespace Web
} // namespace Gateway

#endif // IMIDDLEWARE_H