# Vimb — zentoo fork

This branch (`zentoo`) is a downstream fork of [`fanglingsu/vimb`](https://github.com/fanglingsu/vimb). It rebases onto upstream master periodically.

## Vimb - the Vim-like browser

Vimb is a Vim-like web browser that is inspired by Pentadactyl and Vimprobable.
The goal of Vimb is to build a completely keyboard-driven, efficient and
pleasurable browsing-experience with low memory and CPU usage that is
intuitive to use for Vim users.

More information and some screenshots of Vimb browser in action can be found on
the project page of [Vimb][].

## Features

- it's modal like Vim
- Vim like keybindings - assignable for each browser mode
- nearly every configuration can be changed at runtime with Vim like set syntax
- history for `ex` commands, search queries, URLs
- completions for: commands, URLs, bookmarked URLs, variable names of settings, search-queries
- hinting - marks links, form fields and other clickable elements to
  be clicked, opened or inspected
- SSL validation against ca-certificate file
- user defined URL-shortcuts with placeholders
- read it later queue to collect URIs for later use
- multiple yank/paste registers
- Vim like autocmd - execute commands automatically after an event on specific URIs

## What the fork adds

A stack of single-purpose, rebase-friendly commits on top of upstream:

1. **WebKit-native ad + tracker + cookie-banner blocking.** Adds `VIMB_CONTENT_FILTER_STORE_PATH` to `src/config.def.h` and content-filter loading + `WebKitUserContentManager` attachment in `src/main.c`. Filters are precompiled to WebKit bytecode by [`app-misc/vimb-blocklist`](https://github.com/craig-miller/vimb-blocklist) in the zentoo overlay and loaded synchronously at `vimb_setup()` before any WebView is created.
2. **Opaque WebView background `#141414` (basalt).** `webkit_web_view_set_background_color` on every WebView so the pre-page-load state paints a solid deep near-black instead of WebKit's default opaque white. `GUI_WINDOW_BACKGROUND_COLOR` mirrors the value for the GTK window frame. A CSS-driven opacity fade-in was attempted (see commit `f2067e6`) and reverted: GTK4 CSS opacity doesn't apply to the WebView because WebKitGTK renders through its own Wayland subsurface, which bypasses GTK's snapshot compositor. The solid-black-then-progressive-render behavior is what the code ships.
3. **Home page `https://kagi.com`.** One-line `SETTING_HOME_PAGE` flip. Privacy-respecting search, matches zentoo's content-filter + encrypted-DNS defaults. Users override at runtime via `set home-page=<url>` in `~/.config/vimb/config`.
4. **`SIGUSR2` → reload config across all clients.** New signal handler that re-runs `ex_run_file()` on `~/.config/vimb/config` for every open Client. Deferred via `g_idle_add` so cascading GTK/WebKit signals fire from a clean main-loop iteration. Lets external tools (a system-theme daemon, a dotfile installer) flip runtime settings without restart. `pkill -USR2 -x vimb` broadcasts to every live vimb window.

5. **Dark Reader theming stack.** Adds `resources/etc-vimb-config` (a baseline system config with `dark-mode=on` + the `zm` keybind for manual toggle) and `resources/dr-fixes/` (a fixes-DB refresh script, a runtime bootstrap, and a weekly cron entry) for automatic dark theming of sites that lack native dark-mode support. Layered on top of that, `user_style()` / `user_scripts()` / config sourcing gained a system-file fallback pattern (`/usr/share/vimb/{style.css,scripts.js}`, `/etc/vimb/config`) plus a `/var/lib/vimb/scripts.js` intermediate for cron-driven refreshes to write to. Wire it up via `make install-dark-reader` (below) or the `dark-reader` USE flag (Gentoo overlay ebuild).

6. **Cookie persistence fix.** Upstream vimb's WebKitGTK 6.0 port never binds its network session to WebViews, so cookies live in memory only and vanish on quit — every site logs you out between sessions. This fork wires the session in so `~/.local/share/vimb/cookies.db` actually persists.

7. **Pass-backed password manager.** Native save + autofill wired directly into the browser, backed by `pass(1)`. Entries live under `~/.password-store/websites/<host>/<user>.gpg`. Form detection is a byte-for-byte vendored copy of GNOME Web (Epiphany)'s `ephy.js` — the same battle-tested detector, only the C↔JS binding layer is substituted for vimb's `WebKitUserContentManager` postMessage pattern. Adds `:pass-fill [host[/user]]` and `:pass-forget host/user` ex-commands, `:set pass-enabled=on/off`, and a save-prompt mode (`'P'`) + fill-picker mode (`'F'`) registered via `vb_mode_add`. No dependency on `libsecret` or `pass-secret-service`.

8. **Mode-aware GTK chrome + Noctalia v5 dynamic palette.** New `insert-css` / `command-css` / `hint-mode-css` / `pass-css` / `passthrough-css` settings tint the statusbar per vim mode via CSS classes added by `vb_chrome_set_mode_class` on every mode transition. New `hint-label-css` / `hint-link-css` / `hint-focus-css` settings drive the page-level f-hint labels and highlighted-link colors, overlaid at author level on top of the existing `CSS_HINTS` sheet so the palette wins the cascade. All eight new settings — plus the existing `status-css` / `input-css` / completion trio — are populated by a Noctalia v5 user template (`[theme.templates.user.vimb]`) that renders the active palette's role tokens (`surface`, `primary`, `tertiary`, `error`, ...) into `~/.config/vimb/noctalia-theme`; the template's `post_hook = "pkill -USR2 -x vimb"` fires the SIGUSR2 reload from feature 4, so palette flips, wallpaper regeneration, and dark/light toggle propagate to every open vimb window in real time — no restart. Compile-time defaults switched from the historic lime-green statusbar to a Noctalia-Default (approximately Catppuccin Mocha) dark palette so cold boot without Noctalia rendering still shows coherent colors. Template + registration live in [zentoo-dotfiles](https://github.com/craig-miller/zentoo-dotfiles/blob/main/noctalia/dot-config/noctalia/templates/vimb.tpl); the ebuild ships the vimb side.

9. **Floating cmdline popover (noice.nvim style).** New `src/floating-cmdline.{c,h}` module wraps `vb.notebook` in a `GtkOverlay` and provides a center-screen popover that reparents `vb.inputbox` on demand. Every prompt that previously used the thin bottom-of-window inputbox — `:` ex commands, `/` and `?` search, pass save prompt (mode `'P'`), pass fill picker (mode `'F'`), and the `:open` completion picker — opens in the popover instead, at 2/3 of the window width, with a title label and a completion slot. Focus routing is mode-aware: text-accumulate modes (`'c'` / `'F'`) grab focus onto the inputbox so keystrokes flow through GTK's IM into the buffer; keystroke-dispatch modes leave focus on the webview so keys dispatch through the mode's `keypress` callback. The `:open` completion pane inside the popover now renders URL and title in distinct columns (fixed 50-char URL width, 24px gap) so long titles never squeeze the URL into ellipsis.

10. **fzf-style pass fill picker.** The pass fill picker (mode `'F'`, feature 7) is a `GtkFilterListModel` + `GtkCustomFilter` + `GtkSingleSelection` inside the floating popover: type any substring to filter the loaded pass entries in-place, ↑/↓ walk the filtered rows (wrapping), Enter fills the selection into the page, Esc cancels. Replaces the earlier inline `[1](user) [2](user)…` echo-menu with a real search-and-select surface.

11. **Hint labels: pill shape + text-only highlight.** Baseline `background-color:#ff0 !important;` on the `*[vimbhint^='hint']` selector was stripped from `src/scripts/hints.css` so hinted links show as colored text against the page's own background rather than yellow-filled boxes; the color comes from `hint-link-css` in the Noctalia palette (feature 8). The number pips (`span[vimbhint^='label']`) got `border-radius:999px` and `padding:1px 5px`, so single-digit labels render as circles and multi-digit as pills. Focused (partial-match-in-progress) labels stay fully opaque via the existing `vimbhint='label focus'` rule.

12. **Download subsystem: WebKitGTK 6.0 fix + XDG integration.** Three related changes to `src/main.c` + `src/setting.c`:

    - **`webkit_download_set_destination` API-change fix.** WebKitGTK 6.0 changed the second argument from a `file://` URI to a plain filesystem path. Upstream vimb still constructs a URI via `g_filename_to_uri()`, tripping WebKit's internal `g_path_is_absolute()` assertion and hanging every download of a non-renderable MIME (PDF, tarball, anything WebKit can't render). Fixed at the set site (`vb_download_set_destination`) and symmetrically at the read sites (`on_webdownload_failed`, `on_webdownload_finished`) — the `destination` property is now a plain path too, so the old `g_filename_from_uri()` calls at those sites would produce NULL and lose the basename in completion/error messages.

    - **`download-open` (bool, default on).** New setting. When on, downloads whose MIME type WebKit can't render itself are opened with the default XDG handler (via `g_app_info_get_default_for_type()` — same source as `xdg-mime query default <mime>`) once the download finishes. `application/pdf` → sioyek, `application/x-tar` → whatever's wired up, and so on. A `file://` short-circuit at `decide_response` time skips the pointless copy for local files: the handler is launched directly on the original path with no download at all — matters especially for PDFs whose sibling files (relative links, colocated resources) need the original folder context. A renderability check (`webkit_web_view_can_show_mime_type`) guards user-initiated saves of content WebKit was already rendering (save-page, hint-save on HTML) — those download quietly without also spawning a second viewer window.

    - **`download-path` default from `XDG_DOWNLOAD_DIR`.** Upstream hardcodes `~/Downloads`, silently overriding any custom `XDG_DOWNLOAD_DIR` in `~/.config/user-dirs.dirs`. Cascade at `setting_init` time: `g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD)` → `SETTING_DOWNLOAD_PATH` fallback. Explicit `set download-path=…` in the user's config still overrides (config loads after setting init).

**Why SIGUSR2 rather than SIGUSR1.** WebKit's JSC uses `SIGUSR1` for stop-the-world garbage-collection signaling. Registering our own SIGUSR1 handler prints `Overriding existing handler for signal 10. Set JSC_SIGNAL_FOR_GC if you want WebKit to use a different signal.` at startup and crashes the process (SIGSEGV) on the next GC pass. SIGUSR2 is unclaimed by the WebKit / GLib / GTK stack.

## Installing

### Gentoo Linux
The [zentoo overlay](https://github.com/craig-miller/zentoo-overlay) carries `www-client/vimb` wired to this branch (git-r3 live). If you're following the install guide, `sudo emerge --ask www-client/vimb` pulls the fork, WebKit-GTK 6.0, the ad-blocking helper stack, and installs `/usr/bin/vimb-theme-flip` — a small shell helper for Noctalia's `theme_mode_changed` hook.

### Other distros

Standard build produces a stock vimb binary — none of the zentoo opinions apply until you install the extra bits on top.

```sh
git clone -b zentoo https://github.com/craig-miller/vimb.git
cd vimb
make

# Compose what you want:
sudo make install                # stock vimb
sudo make install-config         # + baseline config: dark-mode=on + zm toggle
sudo make install-dark-reader    # + Dark Reader library + weekly fixes-DB cron
```

`install-dark-reader` chains through `install-config` and installs the full theming stack: the Dark Reader library, a fixes-aware runtime bootstrap, a refresh script, and a `/etc/cron.weekly/vimb-dr-fixes` entry that refreshes per-site fixes weekly from upstream. It fetches the Dark Reader tarball from npm at install time; for offline / packaging use, download `darkreader-4.9.128.tgz` and pass `DR_TARBALL=/path/to/darkreader-4.9.128.tgz` to `make`.

After `install-dark-reader`, populate the state file once so vimb has fixes on next launch:

```sh
sudo /usr/local/libexec/vimb-dr-fixes-refresh
```

#### Cron daemon setup

The weekly refresh script is a shell wrapper in `/etc/cron.weekly/`. Your cron daemon has to be running to pick it up:

- **Arch**: `sudo systemctl enable --now cronie.service`
- **Debian / Ubuntu**: `cron` is usually already running.
- **Alpine**: `sudo rc-update add crond && sudo rc-service crond start`

**Laptop users, install anacron.** Without it, the fixes-DB stays whatever it was on the day the machine was awake at cron time; there's no catch-up on wake. Most distros package `anacron` separately from the cron daemon. Alternatively, some cron daemons ship anacron support behind a build flag — Gentoo's `sys-process/cronie[+anacron]` is one example.

#### Uninstalling

```sh
sudo make uninstall                  # stock vimb
sudo make uninstall-dark-reader      # + Dark Reader + config (chains through uninstall-config)
sudo make uninstall-config           # just the baseline config
```

DEPEND is `webkit-gtk 6` + `gtk 4` + the GStreamer plugin cluster (`good`, `libav`, `opus`, `soup`, `pulse`, `adaptivedemux2`, `dash`, `hls`) — see the [zentoo overlay ebuild](https://github.com/craig-miller/zentoo-overlay/blob/main/www-client/vimb/vimb-9999.ebuild) for the canonical set.

## Rebasing on upstream

```sh
git fetch upstream
git rebase upstream/master
git push origin zentoo   # force-push after rebase
```

Each commit is single-purpose so conflicts, if any, are localized.


