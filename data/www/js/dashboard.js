/**
 * dashboard.js
 * Dashboard page logic: views, status polling, user management.
 */

'use strict';

(function () {

    /* --------------------------------------------------------
       State
       -------------------------------------------------------- */
    let currentView    = 'overview';
    let statusInterval = null;
    let sidebarOpen    = false;

    /* --------------------------------------------------------
       Initialize
       -------------------------------------------------------- */
    function init() {
        // Restore CSRF token
        const csrfToken = sessionStorage.getItem('csrfToken');
        if (csrfToken) {
            GW.setCSRFToken(csrfToken);
        }

        // Restore session user
        const userJson = sessionStorage.getItem('sessionUser');
        if (userJson) {
            try {
                GW.setSessionUser(JSON.parse(userJson));
            } catch { /* ignore */ }
        }

        // Verify session is still valid
        verifySession();
    }

    /* --------------------------------------------------------
       Session verification
       -------------------------------------------------------- */
    async function verifySession() {
        try {
            const res = await GW.api.get('/auth/me');

            if (!res?.data?.user) {
                redirectToLogin();
                return;
            }

            const user = res.data.user;
            GW.setSessionUser(user);

            // Refresh CSRF token
            if (!GW.getCSRFToken()) {
                await refreshCSRF();
            }

            // Setup UI with user data
            setupUserUI(user);
            setupNavigation();
            setupEventListeners();

            // Load initial data
            await loadSystemStatus();
            startStatusPolling();

            // Apply role-based visibility
            applyRoleVisibility();

        } catch (err) {
            if (err?.status === 401) {
                redirectToLogin();
            }
        }
    }

    /* --------------------------------------------------------
       Refresh CSRF token
       -------------------------------------------------------- */
    async function refreshCSRF() {
        try {
            const res = await GW.api.get('/auth/csrf');
            if (res?.data?.csrfToken) {
                GW.setCSRFToken(res.data.csrfToken);
                sessionStorage.setItem('csrfToken', res.data.csrfToken);
            }
        } catch { /* Non-fatal */ }
    }

    /* --------------------------------------------------------
       Setup user UI
       -------------------------------------------------------- */
    function setupUserUI(user) {
        GW.setText('sidebarUsername', user.username);
        GW.setText('sidebarRole', user.role);

        const avatar = document.getElementById('userAvatar');
        if (avatar && user.username) {
            avatar.textContent =
                user.username.charAt(0).toUpperCase();
        }
    }

    /* --------------------------------------------------------
       Navigation (SPA routing)
       -------------------------------------------------------- */
    function setupNavigation() {
        const navLinks = GW.$$('.nav-link[data-view]');

        navLinks.forEach(link => {
            link.addEventListener('click', (e) => {
                e.preventDefault();
                const view = link.dataset.view;
                switchView(view, link);
            });
        });
    }

    function switchView(viewId, clickedLink) {
        // Hide all views
        GW.$$('.view').forEach(v => {
            v.classList.remove('view--active');
        });

        // Show target view
        const targetView = document.getElementById(`view-${viewId}`);
        if (targetView) {
            targetView.classList.add('view--active');
        }

        // Update nav active state
        GW.$$('.nav-link').forEach(l => {
            l.classList.remove('nav-link--active');
            l.removeAttribute('aria-current');
        });

        if (clickedLink) {
            clickedLink.classList.add('nav-link--active');
            clickedLink.setAttribute('aria-current', 'page');
        }

        // Update breadcrumb
        const viewNames = {
            overview: 'Overview',
            users:    'Users',
            devices:  'Devices',
            settings: 'Settings',
        };
        GW.setText('viewTitle', viewNames[viewId] || viewId);

        currentView = viewId;

        // Load view-specific data
        if (viewId === 'users') {
            loadUsers();
        }

        // Close sidebar on mobile
        if (window.innerWidth < 768) {
            closeSidebar();
        }
    }

    /* --------------------------------------------------------
       Event listeners
       -------------------------------------------------------- */
    function setupEventListeners() {
        // Logout
        document.getElementById('logoutBtn')
            ?.addEventListener('click', handleLogout);

        // Refresh status
        document.getElementById('refreshStatusBtn')
            ?.addEventListener('click', loadSystemStatus);

        // Clear events
        document.getElementById('clearEventsBtn')
            ?.addEventListener('click', clearEventLog);

        // Restart
        document.getElementById('restartBtn')
            ?.addEventListener('click', handleRestart);

        // Sidebar toggle
        document.getElementById('sidebarToggle')
            ?.addEventListener('click', closeSidebar);

        document.getElementById('menuBtn')
            ?.addEventListener('click', openSidebar);

        // Modal backdrop close
        document.getElementById('modalBackdrop')
            ?.addEventListener('click', closeModal);
    }

    /* --------------------------------------------------------
       Apply role-based visibility
       -------------------------------------------------------- */
    function applyRoleVisibility() {
        const user = GW.getSessionUser();
        if (!user) return;

        // Hide nav items based on role
        GW.$$('[data-require-role]').forEach(el => {
            const required = el.dataset.requireRole;
            if (!GW.hasRole(required)) {
                el.style.display = 'none';
            }
        });
    }

    /* --------------------------------------------------------
       Load system status
       -------------------------------------------------------- */
    async function loadSystemStatus() {
        try {
            const res = await GW.api.get('/system/status');

            if (!res?.data?.success) return;

            const d = res.data;

            // Stat cards
            GW.setText('statState',   d.system?.state ?? '—');
            GW.setText('statNetwork', d.network?.connected
                ? d.network.ip : 'Disconnected');
            GW.setText('statHeap',    GW.formatBytes(d.system?.freeHeap));
            GW.setText('statUptime',  GW.formatUptime(d.system?.uptime));

            // System info
            GW.setText('infoFirmware', 'Gateway v1.0.0');
            GW.setText('infoHeap',     GW.formatBytes(d.system?.freeHeap));
            GW.setText('infoMinHeap',  GW.formatBytes(d.system?.minFreeHeap));
            GW.setText('infoReset',    `Reason ${d.system?.resetReason}`);
            GW.setText('infoSessions',
                `${d.sessions?.active}/${d.sessions?.max}`);

            // Network info
            GW.setText('infoIP',       d.network?.ip       || '—');
            GW.setText('infoRSSI',     GW.formatRSSI(d.network?.rssi));
            GW.setText('infoHostname', d.network?.hostname || '—');
            GW.setText('infoAP',       d.network?.apActive ? 'Active' : 'Off');

            // Network status badge
            const badge = document.getElementById('networkStatusBadge');
            if (badge) {
                badge.className = d.network?.connected
                    ? 'badge badge--success'
                    : 'badge badge--danger';
                badge.textContent = d.network?.connected
                    ? 'Connected'
                    : 'Disconnected';
            }

        } catch (err) {
            if (err?.status !== 401) {
                GW.toast.show('Failed to load system status', 'warning');
            }
        }
    }

    /* --------------------------------------------------------
       Status polling
       -------------------------------------------------------- */
    function startStatusPolling() {
        if (statusInterval) clearInterval(statusInterval);
        statusInterval = setInterval(loadSystemStatus, 10000);
    }

    /* --------------------------------------------------------
       Load users
       -------------------------------------------------------- */
    async function loadUsers() {
        const tbody = document.getElementById('usersTableBody');
        if (!tbody) return;

        tbody.innerHTML =
            '<tr><td colspan="6" class="table__empty">' +
            'Loading users...</td></tr>';

        try {
            const res = await GW.api.get('/users');

            if (!res?.data?.users?.length) {
                tbody.innerHTML =
                    '<tr><td colspan="6" class="table__empty">' +
                    'No users found.</td></tr>';
                return;
            }

            tbody.innerHTML = res.data.users
                .map(user => buildUserRow(user))
                .join('');

            // Attach action buttons
            tbody.querySelectorAll('[data-action]').forEach(btn => {
                btn.addEventListener('click', handleUserAction);
            });

        } catch {
            tbody.innerHTML =
                '<tr><td colspan="6" class="table__empty">' +
                'Failed to load users.</td></tr>';
        }
    }

    function buildUserRow(user) {
        const statusBadge = user.isActive
            ? '<span class="badge badge--success">Active</span>'
            : '<span class="badge badge--danger">Inactive</span>';

        const lockedBadge = user.isLocked
            ? ' <span class="badge badge--warning">Locked</span>'
            : '';

        const roleColors = {
            'superadmin': 'danger',
            'admin':      'warning',
            'operator':   'info',
            'viewer':     'gray',
        };

        const roleBadge =
            `<span class="badge badge--${
                roleColors[user.role] || 'gray'
            }">${GW.escapeHtml(user.role)}</span>`;

        const currentUser = GW.getSessionUser();
        const isSelf      = currentUser?.userId === user.userId;

        const actions = `
            <div class="btn-group">
                ${GW.hasRole('admin')
                    ? `<button class="btn btn--sm btn--ghost"
                               data-action="edit"
                               data-user-id="${GW.escapeHtml(user.userId)}"
                               aria-label="Edit ${GW.escapeHtml(user.username)}">
                           Edit
                       </button>` : ''}
                ${GW.hasRole('admin') && !isSelf
                    ? `<button class="btn btn--sm btn--danger"
                               data-action="delete"
                               data-user-id="${GW.escapeHtml(user.userId)}"
                               aria-label="Delete ${GW.escapeHtml(user.username)}">
                           Delete
                       </button>` : ''}
            </div>`;

        return `
            <tr>
                <td><strong>${GW.escapeHtml(user.username)}</strong></td>
                <td>${GW.escapeHtml(user.displayName || user.username)}</td>
                <td>${roleBadge}</td>
                <td>${statusBadge}${lockedBadge}</td>
                <td>${GW.formatTimestamp(user.lastLoginAt)}</td>
                <td>${actions}</td>
            </tr>`;
    }

    /* --------------------------------------------------------
       User action dispatch
       -------------------------------------------------------- */
    function handleUserAction(e) {
        const btn    = e.currentTarget;
        const action = btn.dataset.action;
        const userId = btn.dataset.userId;

        if (action === 'delete') {
            confirmDeleteUser(userId,
                btn.getAttribute('aria-label').replace('Delete ', ''));
        } else if (action === 'edit') {
            openEditUserModal(userId);
        }
    }

    async function confirmDeleteUser(userId, username) {
        if (!confirm(`Delete user "${username}"? This cannot be undone.`)) {
            return;
        }

        try {
            await GW.api.delete(`/users/${encodeURIComponent(userId)}`);
            GW.toast.show(`User "${username}" deleted`, 'success');
            await loadUsers();
        } catch (err) {
            GW.toast.show(
                err?.data?.error || 'Failed to delete user', 'error'
            );
        }
    }

    function openEditUserModal(userId) {
        // Placeholder — full modal implementation in next phase
        GW.toast.show('Edit user feature coming soon', 'info');
    }

    /* --------------------------------------------------------
       Event log
       -------------------------------------------------------- */
    function addEventLogEntry(message, type = 'info') {
        const log = document.getElementById('eventLog');
        if (!log) return;

        // Remove empty placeholder
        const empty = log.querySelector('.event-log__empty');
        if (empty) empty.remove();

        const entry  = document.createElement('div');
        entry.className = `event-log__entry event-log__entry--${type}`;

        const time = new Date().toLocaleTimeString(undefined, {
            hour:   '2-digit',
            minute: '2-digit',
            second: '2-digit',
        });

        entry.innerHTML =
            `<span class="event-log__time">${GW.escapeHtml(time)}</span>` +
            `<span>${GW.escapeHtml(message)}</span>`;

        log.appendChild(entry);

        // Max entries
        const entries = log.querySelectorAll('.event-log__entry');
        if (entries.length > 50) {
            entries[0].remove();
        }

        // Auto-scroll to bottom
        log.scrollTop = log.scrollHeight;
    }

    function clearEventLog() {
        const log = document.getElementById('eventLog');
        if (log) {
            log.innerHTML =
                '<p class="event-log__empty">Log cleared.</p>';
        }
    }

    // Expose for websocket.js
    window.GW_addEventLogEntry = addEventLogEntry;

    /* --------------------------------------------------------
       Logout
       -------------------------------------------------------- */
    async function handleLogout() {
        try {
            await GW.api.post('/auth/logout');
        } catch { /* ignore */ }

        sessionStorage.clear();
        window.location.href = '/index.html';
    }

    /* --------------------------------------------------------
       Restart
       -------------------------------------------------------- */
    async function handleRestart() {
        if (!confirm('Restart the gateway? You will be disconnected.')) {
            return;
        }

        try {
            await GW.api.post('/system/restart');
            GW.toast.show('Gateway restarting...', 'warning');
            setTimeout(() => {
                window.location.href = '/index.html';
            }, 5000);
        } catch (err) {
            GW.toast.show(
                err?.data?.error || 'Restart failed', 'error'
            );
        }
    }

    /* --------------------------------------------------------
       Sidebar mobile
       -------------------------------------------------------- */
    function openSidebar() {
        const sb = document.getElementById('sidebar');
        sb?.classList.add('sidebar--open');
        sidebarOpen = true;
        document.getElementById('menuBtn')
            ?.setAttribute('aria-expanded', 'true');
    }

    function closeSidebar() {
        const sb = document.getElementById('sidebar');
        sb?.classList.remove('sidebar--open');
        sidebarOpen = false;
        document.getElementById('menuBtn')
            ?.setAttribute('aria-expanded', 'false');
    }

    /* --------------------------------------------------------
       Modal helpers
       -------------------------------------------------------- */
    function closeModal() {
        document.getElementById('modalBackdrop')
            ?.classList.remove('modal-backdrop--visible');
        GW.$$('.modal').forEach(m => m.remove());
    }

    /* --------------------------------------------------------
       Redirect
       -------------------------------------------------------- */
    function redirectToLogin() {
        sessionStorage.clear();
        window.location.href = '/index.html';
    }

    /* --------------------------------------------------------
       Bootstrap
       -------------------------------------------------------- */
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', init);
    } else {
        init();
    }

}());