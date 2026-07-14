// ============================================================
// IManager.h
// Base interface for all manager components.
// Managers are stateful components that manage resources.
// Unlike services, they may not have their own tasks.
// ============================================================

#pragma once

#ifndef IMANAGER_H
#define IMANAGER_H

#include "../core/ErrorCodes.h"

namespace Gateway {
namespace Interfaces {

class IManager {
public:
    virtual ~IManager() = default;

    [[nodiscard]] virtual Result initialize()   = 0;
    [[nodiscard]] virtual bool   isInitialized() const = 0;
    [[nodiscard]] virtual const char* getName() const  = 0;

protected:
    IManager() = default;

    IManager(const IManager&)            = delete;
    IManager& operator=(const IManager&) = delete;

    IManager(IManager&&)                 = default;
    IManager& operator=(IManager&&)      = default;
};

} // namespace Interfaces
} // namespace Gateway

#endif // IMANAGER_H