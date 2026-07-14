// ============================================================
// main.cpp
// Entry point for the Gateway firmware.
//
// This file intentionally contains minimal logic.
// All initialization is delegated to Application class.
// setup() and loop() are present ONLY because the Arduino
// framework requires them. They are NOT the application.
// ============================================================

#include <Arduino.h>
#include "core/Application.h"

// ============================================================
// Arduino entry point - Initialization only
// ============================================================
void setup() {
    // Application is a singleton that owns the entire firmware.
    // It manages boot sequence, task creation and service lifecycle.
    Gateway::Application::getInstance().begin();
}

// ============================================================
// Arduino loop - intentionally minimal
// The firmware runs via FreeRTOS tasks created in Application.
// This task runs at priority 1 on Core 1 and is essentially idle.
// ============================================================
void loop() {
    // Yield to FreeRTOS scheduler.
    // All real work happens in Application-managed tasks.
    vTaskDelay(pdMS_TO_TICKS(1000));
}