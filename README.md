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

Six commits on top of upstream. Each is single-purpose and rebase-friendly.

1. **WebKit-native ad + tracker + cookie-banner blocking.** Adds `VIMB_CONTENT_FILTER_STORE_PATH` to `src/config.def.h` and content-filter loading + `WebKitUserContentManager` attachment in `src/main.c`. Filters are precompiled to WebKit bytecode by [`app-misc/vimb-blocklist`](https://github.com/craig-miller/vimb-blocklist) in the zentoo overlay and loaded synchronously at `vimb_setup()` before any WebView is created.
2. **Window background `#212121e6`.** One-line `GUI_WINDOW_BACKGROUND_COLOR` flip so the pre-paint flash on every page-load is a translucent dark grey instead of white. Flows through `gdk_rgba_parse` + GTK4 CSS `background-color`.
3. **Home page `https://kagi.com`.** One-line `SETTING_HOME_PAGE` flip. Privacy-respecting search, matches zentoo's content-filter + encrypted-DNS defaults. Users override at runtime via `set home-page=<url>` in `~/.config/vimb/config`.
4. **`FEATURE_NO_TABS` on.** Uncomments the compile-time toggle so `:tabopen`, `gn`, `gN`, etc. spawn new vimb processes instead of in-window tabs. Composes with a scrolling tiling compositor (niri) that manages each URL as its own column.
5. **`--no-maximize` in the shipped `.desktop`.** Vimb calls `gtk_window_maximize()` unconditionally at startup unless this flag is passed. On niri that state bypasses layout gaps and reads as an unmanageable fullscreen window; on other compositors that expect no CSD it can misalign the frame. Baked into `vimb.desktop` so every launcher path inherits it.
6. **`SIGUSR2` → reload config across all clients.** New signal handler that re-runs `ex_run_file()` on `~/.config/vimb/config` for every open Client. Deferred via `g_idle_add` so cascading GTK/WebKit signals fire from a clean main-loop iteration. Lets external tools (a system-theme daemon, a dotfile installer) flip runtime settings without restart. `pkill -USR2 -x vimb` broadcasts to every live vimb window.

**Why SIGUSR2 rather than SIGUSR1.** WebKit's JSC uses `SIGUSR1` for stop-the-world garbage-collection signaling. Registering our own SIGUSR1 handler prints `Overriding existing handler for signal 10. Set JSC_SIGNAL_FOR_GC if you want WebKit to use a different signal.` at startup and crashes the process (SIGSEGV) on the next GC pass. SIGUSR2 is unclaimed by the WebKit / GLib / GTK stack.

## Installing

### Gentoo Linux
The [zentoo overlay](https://github.com/craig-miller/zentoo-overlay) carries `www-client/vimb` wired to this branch (git-r3 live). If you're following the install guide, `sudo emerge --ask www-client/vimb` pulls the fork, WebKit-GTK 6.0, the ad-blocking helper stack, and installs `/usr/bin/vimb-theme-flip` — a small shell helper for Noctalia's `theme_mode_changed` hook.

### Other distros
If you want to consume this branch outside the zentoo overlay:

```sh
git clone -b zentoo https://github.com/craig-miller/vimb.git
cd vimb
make
sudo make install
```

DEPEND is `net-libs/webkit-gtk:6` + `gui-libs/gtk:4` + the GStreamer plugin cluster (`good`, `libav`, `opus`, `soup`, `pulse`, `adaptivedemux2`, `dash`, `hls`) — see the overlay ebuild for the canonical set.

## Rebasing on upstream

```sh
git fetch upstream
git rebase upstream/master
git push origin zentoo   # force-push, keeping the six-commit shape
```

Each commit is single-purpose so conflicts, if any, are localized.


