(function () {
    if (typeof Ephy === 'undefined') return;

    Ephy.log = function () {
        try { console.log.apply(console, ['[vimb-pass]', ...arguments]); } catch (e) { }
    };
    Ephy._ = function (s) { return s; };
    Ephy.shouldRememberPasswords = function () { return true; };
    Ephy.isWebApplication = function () { return false; };

    Ephy.Permission = { UNDECIDED: 0, PERMIT: 1, DENY: 2 };
    Ephy.PermissionType = { SAVE_PASSWORD: 0, COOKIE: 1 };
    Ephy.permissionsManager = {
        permission: function () { return Ephy.Permission.UNDECIDED; },
    };

    const filled = new WeakSet();
    Ephy.autoFill = function (element, value) {
        if (!element) return;
        filled.add(element);
        element.value = value;
        element.dispatchEvent(new Event('input',  { bubbles: true }));
        element.dispatchEvent(new Event('change', { bubbles: true }));
    };
    Ephy.isEdited = function (element) {
        if (!element) return false;
        return !filled.has(element);
    };

    Ephy.showGeneratePasswordFlyout = function () { };

    if (Ephy.PreFillUserMenu) {
        Ephy.PreFillUserMenu = class {
            constructor() { }
            dismiss() { }
        };
    }

    Ephy.queryPassword = function (origin, targetOrigin, username, usernameField, passwordField, promiseID) {
        window.webkit.messageHandlers.PasswordManagerQueryPassword.postMessage({
            origin: origin,
            targetOrigin: targetOrigin,
            username: username,
            usernameField: usernameField,
            passwordField: passwordField,
            promiseID: promiseID,
        });
    };
    Ephy.queryUsernames = function (origin, promiseID) {
        window.webkit.messageHandlers.PasswordManagerQueryUsernames.postMessage({
            origin: origin,
            promiseID: promiseID,
        });
    };

    Ephy.onQueryPasswordReply = function (promiseID, username, password) {
        if (Ephy.passwordManager && Ephy.passwordManager.onQueryResponse)
            Ephy.passwordManager.onQueryResponse(username, password, promiseID);
    };
    Ephy.onQueryUsernamesReply = function (promiseID, users) {
        if (Ephy.passwordManager && Ephy.passwordManager.onQueryUsernamesResponse)
            Ephy.passwordManager.onQueryUsernamesResponse(users, promiseID);
    };

    Ephy.passwordManager = new Ephy.PasswordManager(0, 0);

    /* Serializer stub — FormManager posts to passwordFormFocused using this.
       Epiphany's C-side normally provides a real serializer; we stub with a
       plain object so the postMessage doesn't throw. Main-side registers the
       channel below. */
    var focusSerializer = function (pageID, isInsecure) {
        return { pageID: pageID, isInsecure: isInsecure };
    };

    document.addEventListener('submit', function (event) {
        try {
            if (Ephy.handleFormSubmission)
                Ephy.handleFormSubmission(0, 0, event.target);
        } catch (e) {
            Ephy.log('submit hook error:', e && e.message);
        }
    }, true);

    /* Announce a form to the FormManager. The vendored
       Ephy.formControlsAssociated filters its `elements` array by
       `instanceof HTMLFormElement` — so we MUST include the <form> node
       itself, not just its inputs. */
    function announceForm(form) {
        if (!form || !Ephy.formControlsAssociated) return;
        try {
            Ephy.formControlsAssociated(0, 0, [form].concat(Array.from(form.elements)), focusSerializer);
        } catch (e) {
            Ephy.log('formControlsAssociated error:', e && e.message);
        }
    }

    function announceFormsIn(node) {
        if (!node) return;
        if (node.tagName === 'FORM') { announceForm(node); return; }
        if (!node.querySelectorAll) return;
        for (const f of node.querySelectorAll('form')) announceForm(f);
    }

    function initialScan() {
        Ephy.log('initialScan: found', document.querySelectorAll('form').length, 'form(s)');
        for (const f of document.querySelectorAll('form')) announceForm(f);
    }
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', initialScan, { once: true });
    } else {
        initialScan();
    }

    new MutationObserver(function (mutations) {
        for (const m of mutations) {
            for (const node of m.addedNodes) announceFormsIn(node);
        }
    }).observe(document.documentElement, { childList: true, subtree: true });
})();
