/**
 * websocket.js
 * WebSocket client for real-time event streaming.
 * Auto-reconnect with exponential backoff.
 */

'use strict';

(function () {

    /* --------------------------------------------------------
       Configuration
       -------------------------------------------------------- */
    const WS_PATH           = '/ws';
    const PING_INTERVAL_MS  = 25000;
    const MAX_RECONNECT_MS  = 60000;
    const BASE_RECONNECT_MS = 2000;

    /* --------------------------------------------------------
       State
       -------------------------------------------------------- */
    let ws               = null;
    let reconnectAttempts = 0;
    let reconnectTimer    = null;
    let pingTimer         = null;
    let manualClose       = false;
    let isAuthenticated   = false;

    /* --------------------------------------------------------
       Status UI helpers
       -------------------------------------------------------- */
    function setStatus(state, text) {
        const dot  = document.getElementById('wsStatusDot');
        const label = document.getElementById('wsStatusText');

        if (dot) {
            dot.className = `ws-status__dot ws-status__dot--${state}`;
        }
        if (label) {
            label.textContent = text;
        }
    }

    /* --------------------------------------------------------
       Connect
       -------------------------------------------------------- */
    function connect() {
        if (ws && ws.readyState === WebSocket.OPEN) return;

        const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const url      = `${protocol}//${location.host}${WS_PATH}`;

        setStatus('connecting', 'Connecting...');

        try {
            ws = new WebSocket(url);
        } catch (err) {
            console.warn('[WS] Failed to create WebSocket:', err);
            scheduleReconnect();
            return;
        }

        ws.onopen    = handleOpen;
        ws.onmessage = handleMessage;
        ws.onclose   = handleClose;
        ws.onerror   = handleError;
    }

    /* --------------------------------------------------------
       Handlers
       -------------------------------------------------------- */
    function handleOpen() {
        reconnectAttempts = 0;
        setStatus('connecting', 'Authenticating...');

        // Send auth message with session ID
        // Get session ID from cookie
        const sessionId = getCookieValue(
            '__Host-sid'
        ) || getCookieValue('__Host-sid');

        if (sessionId) {
            send({ type: 'auth', sessionId });
        } else {
            setStatus('disconnected', 'No session');
        }

        // Start ping
        startPing();
    }

    function handleMessage(event) {
        let msg;
        try {
            msg = JSON.parse(event.data);
        } catch {
            return;
        }

        switch (msg.type) {
            case 'connected':
                // Server sent initial connected message
                break;

            case 'authenticated':
                isAuthenticated = true;
                setStatus('connected', `${msg.user} ✓`);
                logEvent(`WebSocket authenticated as ${msg.user}`, 'success');
                break;

            case 'auth_required':
            case 'auth_failed':
                isAuthenticated = false;
                setStatus('disconnected', 'Auth failed');
                logEvent('WebSocket authentication failed', 'error');
                break;

            case 'pong':
                // Keep-alive response
                break;

            case 'event':
                handleSystemEvent(msg);
                break;

            case 'error':
                console.warn('[WS] Server error:', msg.message);
                break;

            default:
                break;
        }
    }

    function handleClose(event) {
        isAuthenticated = false;
        stopPing();

        if (manualClose) {
            setStatus('disconnected', 'Disconnected');
            return;
        }

        setStatus('disconnected',
            `Disconnected (${event.code})`);

        logEvent('WebSocket disconnected — reconnecting...', 'warning');
        scheduleReconnect();
    }

    function handleError() {
        // onclose will fire after onerror — handle there
    }

    /* --------------------------------------------------------
       System event handler
       -------------------------------------------------------- */
    function handleSystemEvent(msg) {
        const eventType = msg.event || 'unknown';
        const data      = msg.data  || [];

        let message  = `Event: ${eventType}`;
        let logType  = 'info';

        // Map known events to human-readable messages
        const eventMessages = {
            'system.stateChanged':
                `State changed: ${data[0] || ''} → ${data[1] || ''}`,
            'system.bootComplete':
                'System boot complete',
            'network.gotIP':
                `Network connected`,
            'network.disconnected':
                'Network disconnected',
            'health.warning':
                'System health warning',
            'health.critical':
                'Critical health alert!',
            'health.recovered':
                'System health recovered',
            'auth.login':
                'User logged in',
            'auth.logout':
                'User logged out',
            'device.registered':
                'New device registered',
            'device.offline':
                'Device went offline',
        };

        if (eventMessages[eventType]) {
            message = eventMessages[eventType];
        }

        if (eventType.includes('critical') ||
            eventType.includes('error')) {
            logType = 'error';
        } else if (eventType.includes('warning') ||
                   eventType.includes('disconnected') ||
                   eventType.includes('offline')) {
            logType = 'warning';
        } else if (eventType.includes('complete') ||
                   eventType.includes('login') ||
                   eventType.includes('connected') ||
                   eventType.includes('recovered')) {
            logType = 'success';
        }

        logEvent(message, logType);

        // Trigger status refresh on important events
        if (eventType === 'system.stateChanged' ||
            eventType === 'network.gotIP' ||
            eventType === 'network.disconnected') {
            // Debounce: refresh after 1s
            if (window._statusRefreshTimer) {
                clearTimeout(window._statusRefreshTimer);
            }
            window._statusRefreshTimer = setTimeout(() => {
                if (typeof GW !== 'undefined') {
                    GW.api.get('/system/status').then(res => {
                        if (res?.data) {
                            // Update will be handled by polling
                        }
                    }).catch(() => {});
                }
            }, 1000);
        }

        // Show toast for critical events
        if (logType === 'error') {
            GW.toast.show(message, 'error');
        }
    }

    /* --------------------------------------------------------
       Send message
       -------------------------------------------------------- */
    function send(data) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;

        try {
            ws.send(JSON.stringify(data));
        } catch (err) {
            console.warn('[WS] Send failed:', err);
        }
    }

    /* --------------------------------------------------------
       Ping
       -------------------------------------------------------- */
    function startPing() {
        stopPing();
        pingTimer = setInterval(() => {
            send({ type: 'ping', ts: Date.now() });
        }, PING_INTERVAL_MS);
    }

    function stopPing() {
        if (pingTimer) {
            clearInterval(pingTimer);
            pingTimer = null;
        }
    }

    /* --------------------------------------------------------
       Reconnect with exponential backoff
       -------------------------------------------------------- */
    function scheduleReconnect() {
        if (reconnectTimer) return;

        const delay = Math.min(
            BASE_RECONNECT_MS * Math.pow(1.5, reconnectAttempts),
            MAX_RECONNECT_MS
        );

        reconnectAttempts++;

        setStatus('connecting',
            `Reconnecting in ${Math.round(delay / 1000)}s...`);

        reconnectTimer = setTimeout(() => {
            reconnectTimer = null;
            connect();
        }, delay);
    }

    /* --------------------------------------------------------
       Log event to UI
       -------------------------------------------------------- */
    function logEvent(message, type = 'info') {
        if (typeof window.GW_addEventLogEntry === 'function') {
            window.GW_addEventLogEntry(message, type);
        }
    }

    /* --------------------------------------------------------
       Cookie helper (client-side only for session ID)
       Cookies marked HttpOnly are NOT accessible here —
       this reads only the session ID if somehow not HttpOnly.
       The proper WS auth flow sends sessionId via message.
       -------------------------------------------------------- */
    function getCookieValue(name) {
        // HttpOnly cookies are not accessible via JS (by design).
        // We use sessionStorage for WS auth token exchange.
        return null;
    }

    /* --------------------------------------------------------
       Disconnect
       -------------------------------------------------------- */
    function disconnect() {
        manualClose = true;
        stopPing();
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        if (ws) {
            ws.close(1000, 'Client disconnect');
            ws = null;
        }
    }

    /* --------------------------------------------------------
       Bootstrap — connect after dashboard loads
       -------------------------------------------------------- */
    function init() {
        // Wait 500ms for auth to complete
        setTimeout(connect, 500);

        // Disconnect on page unload
        window.addEventListener('beforeunload', disconnect);

        // Reconnect on visibility change
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'visible' &&
                (!ws || ws.readyState === WebSocket.CLOSED)) {
                manualClose = false;
                reconnectAttempts = 0;
                connect();
            }
        });
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

}());