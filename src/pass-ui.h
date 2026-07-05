/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2026 Craig Miller
 *
 * Controller layer for the pass(1) password manager. Owns the global
 * VbPassManager instance, registers per-webview UCM script-message
 * handlers, hosts the save-prompt mode ('P'), the fill picker, and the
 * :set pass-enabled toggle.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#ifndef _PASS_UI_H
#define _PASS_UI_H

#include <webkit/webkit.h>
#include "main.h"

/* One-shot init. Constructs the global VbPassManager, warms the
 * origin→usernames cache, initializes the deny-set + pending-save table,
 * and registers the 'P' save-prompt mode via vb_mode_add(). */
void vb_pass_ui_init(void);

/* Per-webview registration — attach the 4 script-message handlers to a
 * UCM. Called from webview_new() alongside the existing focus/scroll
 * handler registrations. */
void vb_pass_ui_register_ucm(WebKitUserContentManager *ucm);

/* :set pass-enabled */
gboolean vb_pass_ui_enabled(void);
void     vb_pass_ui_set_enabled(gboolean enabled);

/* Ex-command entry points — thin delegates from ex.c. */
VbCmdResult vb_pass_ui_fill(Client *c, const char *host_arg);
VbCmdResult vb_pass_ui_forget(Client *c, const char *host_arg);

#endif /* end of include guard: _PASS_UI_H */
