/**
 * auth.js
 * Login page logic.
 * Handles form submission, validation, and redirect.
 */

'use strict';

(function () {

    /* --------------------------------------------------------
       DOM references
       -------------------------------------------------------- */
    const form          = document.getElementById('loginForm');
    const usernameInput = document.getElementById('username');
    const passwordInput = document.getElementById('password');
    const loginBtn      = document.getElementById('loginBtn');
    const loginBtnText  = document.getElementById('loginBtnText');
    const loginSpinner  = document.getElementById('loginBtnSpinner');
    const togglePassBtn = document.getElementById('togglePassword');
    const eyeIcon       = document.getElementById('eyeIcon');

    /* --------------------------------------------------------
       State
       -------------------------------------------------------- */
    let isSubmitting = false;
    let loginAttempts = 0;

    /* --------------------------------------------------------
       Initialize
       -------------------------------------------------------- */
    function init() {
        if (!form) return;

        // Load firmware version
        GW.loadFirmwareInfo();

        // Check if already logged in
        checkExistingSession();

        // Event listeners
        form.addEventListener('submit', handleSubmit);
        togglePassBtn?.addEventListener('click', togglePasswordVisibility);

        // Clear errors on input
        usernameInput?.addEventListener('input', () => {
            GW.setFieldError('username', 'usernameError', '');
            GW.hideAlert('loginAlert');
        });

        passwordInput?.addEventListener('input', () => {
            GW.setFieldError('password', 'passwordError', '');
            GW.hideAlert('loginAlert');
        });

        // Focus username field
        usernameInput?.focus();
    }

    /* --------------------------------------------------------
       Check for existing valid session
       -------------------------------------------------------- */
    async function checkExistingSession() {
        try {
            const res = await GW.api.get('/auth/me');
            if (res?.data?.user) {
                // Already logged in — redirect
                window.location.href = '/dashboard.html';
            }
        } catch {
            // No session — stay on login page
        }
    }

    /* --------------------------------------------------------
       Toggle password visibility
       -------------------------------------------------------- */
    function togglePasswordVisibility() {
        if (!passwordInput || !eyeIcon) return;

        const isHidden = passwordInput.type === 'password';
        passwordInput.type = isHidden ? 'text' : 'password';

        togglePassBtn.setAttribute(
            'aria-label',
            isHidden ? 'Hide password' : 'Show password'
        );

        // Update icon
        if (isHidden) {
            eyeIcon.innerHTML =
                '<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0' +
                ' -11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12' +
                ' 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16' +
                ' 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"/>' +
                '<line x1="1" y1="1" x2="23" y2="23"/>';
        } else {
            eyeIcon.innerHTML =
                '<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>' +
                '<circle cx="12" cy="12" r="3"/>';
        }
    }

    /* --------------------------------------------------------
       Form validation
       -------------------------------------------------------- */
    function validate() {
        let valid = true;

        const username = usernameInput?.value?.trim() ?? '';
        const password = passwordInput?.value ?? '';

        // Username
        const usernameErr = GW.validateUsername(username);
        if (usernameErr) {
            GW.setFieldError('username', 'usernameError', usernameErr);
            valid = false;
        }

        // Password (basic check on login — full policy is server-side)
        if (!password) {
            GW.setFieldError('password', 'passwordError',
                             'Password is required');
            valid = false;
        }

        return valid;
    }

    /* --------------------------------------------------------
       Submit handler
       -------------------------------------------------------- */
    async function handleSubmit(e) {
        e.preventDefault();

        if (isSubmitting) return;

        GW.hideAlert('loginAlert');
        GW.clearFieldErrors(
            ['username', 'usernameError'],
            ['password', 'passwordError']
        );

        if (!validate()) return;

        const username = usernameInput.value.trim();
        const password = passwordInput.value;

        setLoading(true);

        try {
            const res = await GW.api.post('/auth/login', {
                username,
                password,
            }, { timeout: 30000 }); // Longer timeout for PBKDF2

            if (res?.data?.success) {
                // Store CSRF token
                if (res.data.csrfToken) {
                    GW.setCSRFToken(res.data.csrfToken);
                    // Store in sessionStorage for dashboard
                    sessionStorage.setItem(
                        'csrfToken', res.data.csrfToken
                    );
                }

                // Store user info
                if (res.data.user) {
                    sessionStorage.setItem(
                        'sessionUser',
                        JSON.stringify(res.data.user)
                    );
                }

                // Clear password from memory
                passwordInput.value = '';

                // Redirect to dashboard
                window.location.href = '/dashboard.html';
            }

        } catch (err) {
            loginAttempts++;
            passwordInput.value = '';
            passwordInput.focus();

            let message = 'Login failed. Please try again.';

            if (err.status === 401) {
                message = 'Invalid username or password.';
            } else if (err.status === 423) {
                message = 'Account is locked. Contact administrator.';
            } else if (err.status === 429) {
                message = 'Too many attempts. Please wait before retrying.';
            } else if (err.status === 408) {
                message = 'Request timed out. Check your connection.';
            } else if (!navigator.onLine) {
                message = 'No network connection.';
            }

            GW.showAlert('loginAlert', 'loginAlertText',
                          message, 'error');

            // Exponential backoff hint after multiple failures
            if (loginAttempts >= 3) {
                GW.showAlert('loginAlert', 'loginAlertText',
                    message +
                    ` (Attempt ${loginAttempts}/${
                        loginAttempts >= 5
                            ? ' — account may be locked'
                            : ''
                    })`,
                    'error'
                );
            }

        } finally {
            setLoading(false);
        }
    }

    /* --------------------------------------------------------
       Loading state
       -------------------------------------------------------- */
    function setLoading(loading) {
        isSubmitting = loading;

        if (loginBtn)      loginBtn.disabled      = loading;
        if (loginBtnText)  loginBtnText.textContent = loading ? 'Signing in...' : 'Sign In';
        if (loginSpinner)  loginSpinner.className  = loading
            ? 'btn__spinner'
            : 'btn__spinner btn__spinner--hidden';

        if (usernameInput) usernameInput.disabled = loading;
        if (passwordInput) passwordInput.disabled = loading;
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