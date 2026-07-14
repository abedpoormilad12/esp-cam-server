/**
 * app.js
 * Core application utilities shared across all pages.
 * No framework dependencies — pure vanilla JS.
 * Designed for ESP32's limited bandwidth.
 */

'use strict';

/* ============================================================
   Namespace
   ============================================================ */
const GW = (function () {

    /* --------------------------------------------------------
       Constants
       -------------------------------------------------------- */
    const API_BASE    = '/api';
    const CSRF_HEADER = 'X-CSRF-Token';

    /* --------------------------------------------------------
       State
       -------------------------------------------------------- */
    let _csrfToken    = '';
    let _sessionUser  = null;

    /* --------------------------------------------------------
       CSRF management
       -------------------------------------------------------- */
    function setCSRFToken(token) {
        _csrfToken = token || '';
    }

    function getCSRFToken() {
        return _csrfToken;
    }

    /* --------------------------------------------------------
       HTTP client
       Wraps fetch with CSRF, JSON parsing, timeout.
       -------------------------------------------------------- */
    async function request(method, path, body, options = {}) {
        const controller = new AbortController();
        const timeoutId  = setTimeout(
            () => controller.abort(),
            options.timeout || 15000
        );

        const headers = {
            'Content-Type': 'application/json',
            'Accept':       'application/json',
        };

        if (_csrfToken && method !== 'GET') {
            headers[CSRF_HEADER] = _csrfToken;
        }

        const fetchOptions = {
            method:      method.toUpperCase(),
            headers,
            credentials: 'same-origin',
            signal:      controller.signal,
        };

        if (body && method !== 'GET') {
            fetchOptions.body = JSON.stringify(body);
        }

        try {
            const response = await fetch(
                `${API_BASE}${path}`,
                fetchOptions
            );

            clearTimeout(timeoutId);

            let data = null;

            const contentType = response.headers.get('content-type');
            if (contentType && contentType.includes('application/json')) {
                data = await response.json();
            }

            if (!response.ok) {
                const error = new Error(
                    data?.error || data?.message ||
                    `HTTP ${response.status}`
                );
                error.status     = response.status;
                error.data       = data;
                error.statusText = response.statusText;
                throw error;
            }

            return { ok: true, status: response.status, data };

        } catch (err) {
            clearTimeout(timeoutId);

            if (err.name === 'AbortError') {
                throw Object.assign(
                    new Error('Request timeout'),
                    { status: 408 }
                );
            }

            // Handle 401 → redirect to login
            if (err.status === 401) {
                window.location.href = '/index.html';
                return;
            }

            throw err;
        }
    }

    const api = {
        get:    (path, opts)        => request('GET',    path, null,  opts),
        post:   (path, body, opts)  => request('POST',   path, body,  opts),
        put:    (path, body, opts)  => request('PUT',    path, body,  opts),
        delete: (path, opts)        => request('DELETE', path, null,  opts),
    };

    /* --------------------------------------------------------
       Toast notifications
       -------------------------------------------------------- */
    const toast = (function () {
        const container = document.getElementById('toastContainer');

        function show(message, type = 'info', duration = 4000) {
            if (!container) return;

            const el = document.createElement('div');
            el.className = `toast toast--${type}`;
            el.setAttribute('role', 'alert');

            const icon = {
                success: '✓',
                error:   '✕',
                warning: '⚠',
                info:    'ℹ',
            }[type] || 'ℹ';

            el.innerHTML =
                `<span>${escapeHtml(icon)}</span>` +
                `<span>${escapeHtml(message)}</span>`;

            container.appendChild(el);

            setTimeout(() => {
                el.addEventListener(
                    'animationend',
                    () => el.remove(),
                    { once: true }
                );
                el.style.opacity = '0';
                el.style.transform = 'translateY(8px)';
                el.style.transition = 'opacity 300ms, transform 300ms';
            }, duration);
        }

        return { show };
    }());

    /* --------------------------------------------------------
       DOM helpers
       -------------------------------------------------------- */
    function $(selector, ctx = document) {
        return ctx.querySelector(selector);
    }

    function $$(selector, ctx = document) {
        return Array.from(ctx.querySelectorAll(selector));
    }

    function setText(id, text) {
        const el = document.getElementById(id);
        if (el) el.textContent = text ?? '—';
    }

    function setHTML(id, html) {
        const el = document.getElementById(id);
        if (el) el.innerHTML = html;
    }

    function show(id) {
        const el = document.getElementById(id);
        if (el) el.style.display = '';
    }

    function hide(id) {
        const el = document.getElementById(id);
        if (el) el.style.display = 'none';
    }

    /* --------------------------------------------------------
       Security: XSS prevention
       -------------------------------------------------------- */
    function escapeHtml(str) {
        if (typeof str !== 'string') return String(str ?? '');
        return str
            .replace(/&/g,  '&amp;')
            .replace(/</g,  '&lt;')
            .replace(/>/g,  '&gt;')
            .replace(/"/g,  '&quot;')
            .replace(/'/g,  '&#x27;');
    }

    /* --------------------------------------------------------
       Formatting utilities
       -------------------------------------------------------- */
    function formatBytes(bytes) {
        if (bytes === null || bytes === undefined) return '—';
        if (bytes < 1024) return `${bytes} B`;
        if (bytes < 1048576) return `${(bytes / 1024).toFixed(1)} KB`;
        return `${(bytes / 1048576).toFixed(2)} MB`;
    }

    function formatUptime(seconds) {
        if (!seconds) return '—';
        const d = Math.floor(seconds / 86400);
        const h = Math.floor((seconds % 86400) / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = seconds % 60;
        if (d > 0) return `${d}d ${h}h ${m}m`;
        if (h > 0) return `${h}h ${m}m ${s}s`;
        if (m > 0) return `${m}m ${s}s`;
        return `${s}s`;
    }

    function formatTimestamp(unixSeconds) {
        if (!unixSeconds) return '—';
        const d = new Date(unixSeconds * 1000);
        return d.toLocaleString(undefined, {
            year:   'numeric',
            month:  'short',
            day:    'numeric',
            hour:   '2-digit',
            minute: '2-digit',
        });
    }

    function formatRSSI(rssi) {
        if (!rssi) return '—';
        const quality = rssi >= -50 ? 'Excellent'
            : rssi >= -65 ? 'Good'
            : rssi >= -75 ? 'Fair'
            : 'Poor';
        return `${rssi} dBm (${quality})`;
    }

    /* --------------------------------------------------------
       Form validation helpers
       -------------------------------------------------------- */
    function validateUsername(username) {
        if (!username || username.length < 3) {
            return 'Username must be at least 3 characters';
        }
        if (username.length > 32) {
            return 'Username must be 32 characters or less';
        }
        if (!/^[a-zA-Z0-9_-]+$/.test(username)) {
            return 'Username can only contain letters, numbers, _ and -';
        }
        return null;
    }

    function validatePassword(password) {
        if (!password || password.length < 8) {
            return 'Password must be at least 8 characters';
        }
        if (password.length > 64) {
            return 'Password must be 64 characters or less';
        }
        if (!/[A-Z]/.test(password)) {
            return 'Password must contain at least one uppercase letter';
        }
        if (!/[a-z]/.test(password)) {
            return 'Password must contain at least one lowercase letter';
        }
        if (!/[0-9]/.test(password)) {
            return 'Password must contain at least one number';
        }
        return null;
    }

    function setFieldError(fieldId, errorId, message) {
        const field = document.getElementById(fieldId);
        const error = document.getElementById(errorId);

        if (field) {
            if (message) {
                field.classList.add('form-input--error');
                field.setAttribute('aria-invalid', 'true');
            } else {
                field.classList.remove('form-input--error');
                field.removeAttribute('aria-invalid');
            }
        }

        if (error) {
            error.textContent = message || '';
        }
    }

    function clearFieldErrors(...pairs) {
        pairs.forEach(([fieldId, errorId]) => {
            setFieldError(fieldId, errorId, '');
        });
    }

    /* --------------------------------------------------------
       Alert component
       -------------------------------------------------------- */
    function showAlert(alertId, textId, message, type = 'error') {
        const alert = document.getElementById(alertId);
        const text  = document.getElementById(textId);

        if (!alert || !text) return;

        text.textContent = message;
        alert.className  = `alert alert--${type}`;
        alert.removeAttribute('style');
    }

    function hideAlert(alertId) {
        const alert = document.getElementById(alertId);
        if (alert) alert.className = 'alert alert--hidden';
    }

    /* --------------------------------------------------------
       Session helpers
       -------------------------------------------------------- */
    function setSessionUser(user) {
        _sessionUser = user;
    }

    function getSessionUser() {
        return _sessionUser;
    }

    function hasRole(requiredRole) {
        if (!_sessionUser) return false;
        const roleOrder = {
            'none': 0, 'viewer': 1, 'operator': 2,
            'admin': 3, 'superadmin': 4
        };
        const userLevel = roleOrder[_sessionUser.role] ?? 0;
        const reqLevel  = roleOrder[requiredRole] ?? 0;
        return userLevel >= reqLevel;
    }

    /* --------------------------------------------------------
       Load firmware info on auth page
       -------------------------------------------------------- */
    async function loadFirmwareInfo() {
        try {
            const res = await api.get('/system/info');
            if (res?.data?.firmware) {
                const fw = res.data.firmware;
                setText('firmwareVersion',
                    `${fw.project} v${fw.version} (${fw.build})`);
            }
        } catch {
            // Non-critical — silent fail
        }
    }

    /* --------------------------------------------------------
       Public API
       -------------------------------------------------------- */
    return {
        api,
        toast,
        $,
        $$,
        setText,
        setHTML,
        show,
        hide,
        escapeHtml,
        formatBytes,
        formatUptime,
        formatTimestamp,
        formatRSSI,
        validateUsername,
        validatePassword,
        setFieldError,
        clearFieldErrors,
        showAlert,
        hideAlert,
        setCSRFToken,
        getCSRFToken,
        setSessionUser,
        getSessionUser,
        hasRole,
        loadFirmwareInfo,
    };

}());