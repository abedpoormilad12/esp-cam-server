// ============================================================
// IStorage.h
// Abstract interface for all storage backends.
// Allows swapping storage implementations without
// changing application-layer code.
// ============================================================

#pragma once

#ifndef ISTORAGE_H
#define ISTORAGE_H

#include "../core/ErrorCodes.h"
#include <cstdint>
#include <cstddef>

namespace Gateway {
namespace Interfaces {

class IStorage {
public:
    virtual ~IStorage() = default;

    [[nodiscard]] virtual Result initialize()   = 0;
    [[nodiscard]] virtual bool   isReady() const = 0;

    // Key-value interface
    [[nodiscard]] virtual Result readString(
        const char* key,
        char*       outBuffer,
        size_t      bufferSize
    ) = 0;

    [[nodiscard]] virtual Result writeString(
        const char* key,
        const char* value
    ) = 0;

    [[nodiscard]] virtual Result readUInt32(
        const char*  key,
        uint32_t&    outValue
    ) = 0;

    [[nodiscard]] virtual Result writeUInt32(
        const char* key,
        uint32_t    value
    ) = 0;

    [[nodiscard]] virtual Result readBytes(
        const char* key,
        uint8_t*    outBuffer,
        size_t      bufferSize,
        size_t&     outBytesRead
    ) = 0;

    [[nodiscard]] virtual Result writeBytes(
        const char*    key,
        const uint8_t* data,
        size_t         dataSize
    ) = 0;

    [[nodiscard]] virtual Result remove(const char* key)        = 0;
    [[nodiscard]] virtual Result exists(const char* key,
                                        bool& outExists)        = 0;
    [[nodiscard]] virtual Result clear()                        = 0;

    [[nodiscard]] virtual const char* getName() const           = 0;

protected:
    IStorage() = default;

    IStorage(const IStorage&)            = delete;
    IStorage& operator=(const IStorage&) = delete;
};

} // namespace Interfaces
} // namespace Gateway

#endif // ISTORAGE_H