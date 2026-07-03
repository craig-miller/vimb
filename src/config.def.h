/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2012-2018 Daniel Carl
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

/* features */
/* show wget style progressbar in status bar */
#define FEATURE_WGET_PROGRESS_BAR
/* show load progress in window title */
#define FEATURE_TITLE_PROGRESS
/* show page title in url completions */
#define FEATURE_TITLE_IN_COMPLETION
/* enable the read it later queue */
#define FEATURE_QUEUE
/* disable X window embedding */
/* #define FEATURE_NO_XEMBED */
/* don't write the home-page uri in the history file */
/* #define FEATURE_HISTORY_WITHOUT_HOME_PAGE */

#ifdef FEATURE_WGET_PROGRESS_BAR
/* chars to use for the progressbar */
#define PROGRESS_BAR             "=> "
#define PROGRESS_BAR_LEN            20
#endif

/* disable tabs - all tab commands spawn new browser instances instead */
#define FEATURE_NO_TABS

#define FEATURE_AUTOCMD

/* time in seconds after that message will be removed from inputbox if the
 * message where only temporary */
#define MESSAGE_TIMEOUT             5

/* number of chars to be shown in statusbar for ambiguous commands */
#define SHOWCMD_LEN                 10
/* css applied to the gui elements regardless of user's settings */
#define GUI_STYLE_CSS_BASE          "#input text{background-color:inherit;color:inherit;caret-color:@color;font:inherit;}"
/* define this to set the initial background color for the GTK window */
#define GUI_WINDOW_BACKGROUND_COLOR "#212121e6"

#define INCSEARCH_MATCHES_LIMIT 1000

/* default font size for fonts in webview */
#define SETTING_DEFAULT_FONT_SIZE             16
#define SETTING_DEFAULT_MONOSPACE_FONT_SIZE   13
#define SETTING_GUI_FONT_NORMAL               "font-size:10pt;font-family:monospace;"
#define SETTING_GUI_FONT_EMPH                 "font-weight:bold;font-size:10pt;font-family:monospace;"
#define SETTING_HOME_PAGE                     "https://kagi.com"
#define SETTING_DOWNLOAD_PATH                 "~/Downloads"
/* cookie-accept allowed values always, origin, never */
#define SETTING_COOKIE_ACCEPT                 "always"
#define SETTING_HINT_KEYS                     "0123456789"
#define SETTING_HISTIGNORE                    "^(about:)|(file:)"
#define SETTING_DOWNLOAD_COMMAND              "/bin/sh -c \"curl -sLJOC - -e '$VIMB_URI' %s\""
#define SETTING_COMPLETION_CSS                "color:#fff;background-color:#656565;" SETTING_GUI_FONT_NORMAL
#define SETTING_COMPLETION_HOVER_CSS          "background-color:#777;"
#define SETTING_COMPLETION_SELECTED_CSS       "color:#f6f3e8;background-color:#888;"
#define SETTING_INPUT_CSS                     "background-color:#fff;color:#000;" SETTING_GUI_FONT_NORMAL
#define SETTING_INPUT_ERROR_CSS               "background-color:#f77;" SETTING_GUI_FONT_EMPH
#define SETTING_STATUS_CSS                    "color:#fff;background-color:#000;" SETTING_GUI_FONT_EMPH
#define SETTING_STATUS_SSL_CSS                "background-color:#95e454;color:#000;"
#define SETTING_STATUS_SSL_INVLID_CSS         "background-color:#f77;color:#000;"

/* System-wide default paths — consulted only when the corresponding
 * per-user file at ~/.config/vimb/ is ABSENT. When the user file exists
 * (even zero bytes), it wins — an empty user file is the explicit opt-out.
 *
 * SYSTEM_STYLE and SYSTEM_SCRIPT are PREFIX-relative (PREFIX injected via
 * CPPFLAGS from the Makefile's RUNPREFIX). SYSTEM_SCRIPT_LOCAL and
 * SYSTEM_CONFIG live at fixed FHS locations. SYSTEM_SCRIPT has an extra
 * middle layer at SYSTEM_SCRIPT_LOCAL for a state-directory copy
 * maintained by a separate package (e.g. cron-driven refreshes).
 * Consultation order: user file -> SCRIPT_LOCAL -> SCRIPT.
 *
 * Override any of these by defining them before this header is included
 * (savedconfig / -D on the compiler command line) — the #ifndef guards
 * respect that. */
#ifndef SYSTEM_STYLE
#define SYSTEM_STYLE         PREFIX "/share/vimb/style.css"
#endif
#ifndef SYSTEM_SCRIPT
#define SYSTEM_SCRIPT        PREFIX "/share/vimb/scripts.js"
#endif
#define SYSTEM_SCRIPT_LOCAL  "/var/lib/vimb/scripts.js"
#define SYSTEM_CONFIG        "/etc/vimb/config"

#define MAXIMUM_HINTS              500
/* default window dimensions */
#define WIN_WIDTH                  800
#define WIN_HEIGHT                 600

/* if set to 1 vimb will check if the webextension could be found. */
#define CHECK_WEBEXTENSION_ON_STARTUP 1

/* This status indicator is only shown if "status-bar-show-settings" is
 * enabled.
 * The CHAR_MAP(value, internalValue, outputValue, valueIfNotMapped) is a
 * little workaround to translate internal used string value like for
 * GET_CHAR(c, "cookie-accept") which is one of "always", "origin" or "never"
 * to those values that should be shown on statusbar.
 * The STATUS_VARAIBLE_SHOW is used as argument for a printf like function. So
 * the first argument is the output pattern. */
/*
#define STATUS_VARAIBLE_SHOW "js: %s, cookies: %s, hint-timeout: %d", \
    GET_BOOL(c, "scripts") ? "on" : "off", \
    GET_CHAR(c, "cookie-accept"), \
    GET_INT(c, "hint-timeout")
*/
#define COOKIE GET_CHAR(c, "cookie-accept")
#define CHAR_MAP(v, i, m, d) (strcmp(v, i) == 0 ? m : (d))
#define STATUS_VARAIBLE_SHOW "%c%c%c%c%c%c%c%c", \
    CHAR_MAP(COOKIE, "always", 'A', CHAR_MAP(COOKIE, "origin", '@', 'a')), \
    GET_BOOL(c, "dark-mode") ? 'D' : 'd', \
    vb.incognito ? 'E' : 'e', \
    GET_BOOL(c, "images") ? 'I' : 'i', \
    GET_BOOL(c, "html5-local-storage") ? 'L' : 'l', \
    GET_BOOL(c, "stylesheet") ? 'M' : 'm', \
    GET_BOOL(c, "scripts") ? 'S' : 's', \
    GET_BOOL(c, "strict-ssl") ? 'T' : 't'

/* Ad + tracker + cookie-banner blocking via WebKit's native content-filter
 * API. At startup vimb reads three precompiled filters (easylist,
 * easyprivacy, cookies) from this directory and attaches them to every
 * WebView it creates. The store is populated by app-misc/vimb-blocklist
 * in the zentoo overlay via a weekly cron. Set to the empty string to
 * disable filtering. */
#define VIMB_CONTENT_FILTER_STORE_PATH "/var/cache/vimb-blocklist/store"
