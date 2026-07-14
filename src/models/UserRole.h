// ============================================================
// UserRole.h
// Role and permission definitions for the RBAC system.
//
// Design decisions:
//   - Role-Based Access Control (RBAC)
//   - Hierarchical roles: each role includes permissions
//     of all roles below it
//   - Permission is a bitmask: O(1) check, no heap
//   - Resource + Action model for fine-grained control
//   - All permission checks happen server-side
//   - Client receives role info for UI rendering only
//     (never trust client-side role checks)
// ============================================================

#pragma once

#ifndef USER_ROLE_H
#define USER_ROLE_H

#include <cstdint>

namespace Gateway {
namespace Models {

// ============================================================
// User roles — ordered by privilege level
// ============================================================
enum class UserRole : uint8_t {
    NONE        = 0,    // Unauthenticated
    VIEWER      = 1,    // Read-only access
    OPERATOR    = 2,    // Device control
    ADMIN       = 3,    // Full system access
    SUPERADMIN  = 4     // Factory reset, firmware update
};

// ============================================================
// Permission bitmask — 32 bits = 32 distinct permissions
// ============================================================
enum class Permission : uint32_t {
    NONE                    = 0x00000000,

    // Dashboard
    VIEW_DASHBOARD          = 0x00000001,

    // Device management
    VIEW_DEVICES            = 0x00000002,
    CONTROL_DEVICES         = 0x00000004,
    MANAGE_DEVICES          = 0x00000008,

    // User management
    VIEW_USERS              = 0x00000010,
    CREATE_USER             = 0x00000020,
    EDIT_USER               = 0x00000040,
    DELETE_USER             = 0x00000080,
    CHANGE_OWN_PASSWORD     = 0x00000100,

    // System
    VIEW_SYSTEM_STATUS      = 0x00000200,
    VIEW_LOGS               = 0x00000400,
    RESTART_SYSTEM          = 0x00000800,
    MANAGE_CONFIG           = 0x00001000,

    // Sensors / Cameras (future)
    VIEW_SENSORS            = 0x00002000,
    VIEW_CAMERAS            = 0x00004000,
    MANAGE_CAMERAS          = 0x00008000,

    // Security
    MANAGE_SECURITY         = 0x00010000,

    // OTA
    PERFORM_OTA             = 0x00020000,

    // Factory
    FACTORY_RESET           = 0x00040000,

    // All permissions
    ALL                     = 0xFFFFFFFF
};

// ============================================================
// Permission set operations
// ============================================================
inline constexpr uint32_t toUInt(Permission p) noexcept {
    return static_cast<uint32_t>(p);
}

inline constexpr Permission operator|(Permission a,
                                      Permission b) noexcept {
    return static_cast<Permission>(toUInt(a) | toUInt(b));
}

inline constexpr Permission operator&(Permission a,
                                      Permission b) noexcept {
    return static_cast<Permission>(toUInt(a) & toUInt(b));
}

inline constexpr bool hasPermission(Permission set,
                                     Permission required) noexcept {
    return (toUInt(set) & toUInt(required)) == toUInt(required);
}

// ============================================================
// Default permission sets per role
// ============================================================
inline constexpr Permission permissionsForRole(UserRole role) noexcept {
    switch (role) {
        case UserRole::VIEWER:
            return Permission::VIEW_DASHBOARD
                 | Permission::VIEW_DEVICES
                 | Permission::VIEW_SENSORS
                 | Permission::VIEW_CAMERAS
                 | Permission::VIEW_SYSTEM_STATUS
                 | Permission::CHANGE_OWN_PASSWORD;

        case UserRole::OPERATOR:
            return permissionsForRole(UserRole::VIEWER)
                 | Permission::CONTROL_DEVICES;

        case UserRole::ADMIN:
            return permissionsForRole(UserRole::OPERATOR)
                 | Permission::MANAGE_DEVICES
                 | Permission::VIEW_USERS
                 | Permission::CREATE_USER
                 | Permission::EDIT_USER
                 | Permission::DELETE_USER
                 | Permission::VIEW_LOGS
                 | Permission::RESTART_SYSTEM
                 | Permission::MANAGE_CONFIG
                 | Permission::MANAGE_CAMERAS
                 | Permission::MANAGE_SECURITY;

        case UserRole::SUPERADMIN:
            return Permission::ALL;

        default:
            return Permission::NONE;
    }
}

// ============================================================
// String conversions
// ============================================================
inline constexpr const char* roleToString(UserRole role) noexcept {
    switch (role) {
        case UserRole::NONE:       return "none";
        case UserRole::VIEWER:     return "viewer";
        case UserRole::OPERATOR:   return "operator";
        case UserRole::ADMIN:      return "admin";
        case UserRole::SUPERADMIN: return "superadmin";
        default:                   return "unknown";
    }
};

inline constexpr UserRole roleFromString(const char* str) noexcept {
    if (!str) return UserRole::NONE;
    if (__builtin_strcmp(str, "viewer")     == 0) return UserRole::VIEWER;
    if (__builtin_strcmp(str, "operator")   == 0) return UserRole::OPERATOR;
    if (__builtin_strcmp(str, "admin")      == 0) return UserRole::ADMIN;
    if (__builtin_strcmp(str, "superadmin") == 0) return UserRole::SUPERADMIN;
    return UserRole::NONE;
};

}; // namespace Models
}; // namespace Gateway

#endif // USER_ROLE_H