/*
 * vimb Dark Reader bootstrap
 *
 * Runs after DarkReader (bundled by the vimb ebuild) in
 * /var/lib/vimb/scripts.js. Consumes DR_FIXES — the JSON produced by
 * /usr/libexec/vimb-dr-fixes-refresh from upstream's dynamic-theme-fixes +
 * dark-sites .config files.
 *
 * Behavior per page load:
 *   1. If the page is on a native-dark site, do nothing.
 *   2. Otherwise collect the common fix + every matching site fix,
 *      merge them into one DynamicThemeFix, hand to DarkReader.auto().
 *
 * The matcher is a simpler version of DR's own URL-trie: hostname (exact,
 * or *.suffix), plus optional path prefix. IP addresses, ports, and
 * regex-shaped patterns fall through as no-match — the common fix still
 * applies. This is a deliberate simplification; failure mode is "site
 * looks like it did before this refresh", never a broken page.
 */
(function () {
    const DR_FIXES = __DR_FIXES__; /* replaced by refresh script */
    const host = location.hostname.toLowerCase();
    const bareHost = host.replace(/^www\./, '');
    const path = location.pathname;

    function matches(pattern) {
        const slash = pattern.indexOf('/');
        const phost = slash === -1 ? pattern : pattern.slice(0, slash);
        const ppath = slash === -1 ? null : pattern.slice(slash);

        let hostOk;
        if (phost === '*') {
            hostOk = true;
        } else if (phost.startsWith('*.')) {
            const suffix = phost.slice(2);
            hostOk = host === suffix || host.endsWith('.' + suffix);
        } else {
            hostOk = host === phost || bareHost === phost;
        }
        if (!hostOk) return false;
        return ppath === null || path.startsWith(ppath);
    }

    for (const p of DR_FIXES.darkSites) {
        if (matches(p)) return;
    }

    const applied = [DR_FIXES.commonFix];
    for (const fix of DR_FIXES.siteFixes) {
        if (fix.urls.some(matches)) applied.push(fix);
    }

    const merged = {
        invert: [], css: '',
        ignoreInlineStyle: [], ignoreImageAnalysis: [],
        disableStyleSheetsProxy: false, ignoreCSSUrl: [],
    };
    for (const f of applied) {
        if (f.invert) merged.invert.push(...f.invert);
        if (f.css) merged.css += (merged.css ? '\n' : '') + f.css;
        if (f.ignoreInlineStyle) merged.ignoreInlineStyle.push(...f.ignoreInlineStyle);
        if (f.ignoreImageAnalysis) merged.ignoreImageAnalysis.push(...f.ignoreImageAnalysis);
        if (f.disableStyleSheetsProxy) merged.disableStyleSheetsProxy = true;
        if (f.ignoreCSSUrl) merged.ignoreCSSUrl.push(...f.ignoreCSSUrl);
    }

    DarkReader.auto(
        { brightness: 100, contrast: 100, sepia: 0 },
        merged,
    );
})();
