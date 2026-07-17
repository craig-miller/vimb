/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2026 Craig Miller
 *
 * pass(1) password manager — controller layer.
 *
 * See pass-manager.[ch] for the storage model, scripts/pass-detector.js
 * (vendored from Epiphany) for form detection, and scripts/pass-detector-hooks.js
 * for the C↔JS wire glue.
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

#include <string.h>
#include <gio/gio.h>
#include <jsc/jsc.h>

#include "ascii.h"
#include "pass-ui.h"
#include "pass-manager.h"
#include "floating-cmdline.h"
#include "ext-proxy.h"

extern struct Vimb vb;

/* ---------- state ----------------------------------------------------- */

static VbPassManager *manager    = NULL;
static gboolean       enabled    = TRUE;
static GHashTable    *deny_set   = NULL;  /* host (char*) -> gpointer(1) */
static GHashTable    *pending    = NULL;  /* Client* -> PendingSave* */

typedef struct {
    char *origin;
    char *target_origin;
    char *username;
    char *password;
    char *username_field;
    char *password_field;
    gboolean is_new;
} PendingSave;

static void pending_save_free(gpointer p)
{
    PendingSave *s = p;
    if (!s) return;
    g_free(s->origin);
    g_free(s->target_origin);
    g_free(s->username);
    g_free(s->password);
    g_free(s->username_field);
    g_free(s->password_field);
    g_free(s);
}

/* Fill-picker state — single-tab at a time for phase 1. */
typedef struct {
    Client *client;
    GList *records;                  /* GList<VbPassRecord*> owned by us */
    GtkWidget *scrolled;             /* GtkScrolledWindow packed into popover slot */
    GtkWidget *listview;
    GtkSingleSelection *selection;
    GtkFilterListModel *filter_model;
    GtkCustomFilter *filter;
    GtkStringList *store;            /* backing list of username strings */
    GString *needle;                 /* current filter text */
} FillCtx;
static FillCtx *fill_ctx = NULL;

/* Forward decls — mode entry points defined lower in the file but
 * referenced from vb_pass_ui_init. */
static void     fill_picker_enter(Client *c);
static void     fill_picker_leave(Client *c);
static VbResult fill_picker_keypress(Client *c, int key);
static void     fill_picker_input_changed(Client *c, const char *text);

static void fill_ctx_reset(void)
{
    if (!fill_ctx) return;
    /* Unparent the scrolled window — that drops the container's ref chain
     * on listview/selection/filter_model/store, which then die by refcount. */
    if (fill_ctx->scrolled) gtk_widget_unparent(fill_ctx->scrolled);
    if (fill_ctx->needle)   g_string_free(fill_ctx->needle, TRUE);
    g_list_free_full(fill_ctx->records, (GDestroyNotify)vb_pass_record_free);
    g_free(fill_ctx);
    fill_ctx = NULL;
}

/* ---------- helpers --------------------------------------------------- */

static Client *
client_for_ucm(WebKitUserContentManager *ucm)
{
    for (Client *c = vb.clients; c; c = c->next) {
        if (webkit_web_view_get_user_content_manager(c->webview) == ucm)
            return c;
    }
    return NULL;
}

static const char *
str_or_empty(JSCValue *obj, const char *prop)
{
    static char scratch[4096];
    if (!jsc_value_is_object(obj)) return "";
    JSCValue *v = jsc_value_object_get_property(obj, prop);
    if (!v || jsc_value_is_null(v) || jsc_value_is_undefined(v)) {
        if (v) g_object_unref(v);
        scratch[0] = '\0';
        return scratch;
    }
    char *s = jsc_value_to_string(v);
    g_object_unref(v);
    g_strlcpy(scratch, s ? s : "", sizeof scratch);
    g_free(s);
    return scratch;
}

/* Escape a string for embedding inside a JS single-quoted literal. */
static char *
js_escape(const char *s)
{
    if (!s) return g_strdup("");
    GString *out = g_string_sized_new(strlen(s) + 8);
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\\': g_string_append(out, "\\\\"); break;
            case '\'': g_string_append(out, "\\'");  break;
            case '\n': g_string_append(out, "\\n");  break;
            case '\r': g_string_append(out, "\\r");  break;
            case '\t': g_string_append(out, "\\t");  break;
            case '\0': /* impossible mid-string, but be safe */ break;
            default:   g_string_append_c(out, *p);   break;
        }
    }
    return g_string_free(out, FALSE);
}

static gboolean
is_denied(const char *host)
{
    if (!host || !*host) return FALSE;
    return g_hash_table_contains(deny_set, host);
}

static char *
host_from_uri(const char *uri)
{
    if (!uri || !*uri) return NULL;
    GUri *u = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!u) return NULL;
    const char *h = g_uri_get_host(u);
    char *out = (h && *h) ? g_strdup(h) : NULL;
    g_uri_unref(u);
    return out;
}

/* ---------- reply injection ------------------------------------------ */

/* Send Ephy.onQueryPasswordReply(id, user, pass) or (id, null, null). */
static void
reply_query_password(Client *c, int promise_id, const char *user, const char *pass)
{
    char *ue = js_escape(user ? user : "");
    char *pe = js_escape(pass ? pass : "");
    char *js;
    if (pass) {
        js = g_strdup_printf("Ephy.onQueryPasswordReply(%d,'%s','%s');",
                             promise_id, ue, pe);
    } else {
        js = g_strdup_printf("Ephy.onQueryPasswordReply(%d,null,null);",
                             promise_id);
    }
    ext_proxy_eval_script_in_page(c, js);
    g_free(js);
    g_free(ue);
    g_free(pe);
}

/* Send Ephy.onQueryUsernamesReply(id, [names]). */
static void
reply_query_usernames(Client *c, int promise_id, GList *usernames)
{
    GString *arr = g_string_new("[");
    gboolean first = TRUE;
    for (GList *l = usernames; l; l = l->next) {
        const char *slug = l->data;
        /* Filter the null-username slug '_' from the picker list. */
        if (g_strcmp0(slug, "_") == 0) continue;
        if (!first) g_string_append_c(arr, ',');
        first = FALSE;
        char *esc = js_escape(slug);
        g_string_append_printf(arr, "'%s'", esc);
        g_free(esc);
    }
    g_string_append_c(arr, ']');

    char *js = g_strdup_printf("Ephy.onQueryUsernamesReply(%d,%s);",
                               promise_id, arr->str);
    ext_proxy_eval_script_in_page(c, js);
    g_free(js);
    g_string_free(arr, TRUE);
}

/* ---------- fill picker widgets (fzf-style) --------------------------- */

static void
fill_row_setup(GtkListItemFactory *f, GtkListItem *item, gpointer u)
{
    (void)f; (void)u;
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_list_item_set_child(item, label);
}

static void
fill_row_bind(GtkListItemFactory *f, GtkListItem *item, gpointer u)
{
    (void)f; (void)u;
    GtkStringObject *so = gtk_list_item_get_item(item);
    GtkWidget *label = gtk_list_item_get_child(item);
    gtk_label_set_text(GTK_LABEL(label), gtk_string_object_get_string(so));
}

/* Case-insensitive substring match. Empty needle = match everything. */
static gboolean
fill_filter_match(gpointer item, gpointer user_data)
{
    FillCtx *ctx = user_data;
    if (!ctx || !ctx->needle || ctx->needle->len == 0) return TRUE;
    const char *s = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    if (!s) return FALSE;
    char *hay_lc  = g_ascii_strdown(s, -1);
    char *need_lc = g_ascii_strdown(ctx->needle->str, -1);
    gboolean hit  = strstr(hay_lc, need_lc) != NULL;
    g_free(hay_lc);
    g_free(need_lc);
    return hit;
}

/* Build picker widgets from fill_ctx->records and pack into the popover
 * completion slot. Prereq: fill_ctx->records populated. */
static void
fill_picker_build_widgets(void)
{
    FillCtx *ctx = fill_ctx;

    ctx->needle = g_string_new("");
    ctx->store  = gtk_string_list_new(NULL);
    for (GList *l = ctx->records; l; l = l->next) {
        VbPassRecord *r = l->data;
        gtk_string_list_append(ctx->store,
            r->username && *r->username ? r->username : "(no-user)");
    }

    ctx->filter = gtk_custom_filter_new(fill_filter_match, ctx, NULL);
    ctx->filter_model = gtk_filter_list_model_new(
        G_LIST_MODEL(ctx->store), GTK_FILTER(ctx->filter));

    ctx->selection = gtk_single_selection_new(G_LIST_MODEL(ctx->filter_model));
    gtk_single_selection_set_autoselect(ctx->selection, TRUE);
    gtk_single_selection_set_can_unselect(ctx->selection, FALSE);

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(fill_row_setup), NULL);
    g_signal_connect(factory, "bind",  G_CALLBACK(fill_row_bind),  NULL);

    ctx->listview = gtk_list_view_new(GTK_SELECTION_MODEL(ctx->selection), factory);
    /* Reuse completion CSS name so palette theming applies. */
    gtk_widget_set_name(ctx->listview, "completion");

    ctx->scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ctx->scrolled), ctx->listview);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(ctx->scrolled), 240);

    GtkWidget *slot = vb_floating_get_completion_slot();
    if (slot) gtk_box_append(GTK_BOX(slot), ctx->scrolled);
}

/* ---------- save prompt mode ('P') ----------------------------------- */

static void
pass_prompt_enter(Client *c)
{
    vb_modelabel_update(c, "-- PASS --");
    vb_floating_open("Save password?");
    PendingSave *s = g_hash_table_lookup(pending, c);
    if (!s) {
        vb_enter(c, 'n');
        return;
    }
    char *host = host_from_uri(s->origin);
    vb_echo_force(c, MSG_NORMAL, FALSE,
                  "Save password for %s%s%s? (y/n)",
                  host ? host : s->origin,
                  s->username ? " user " : "",
                  s->username ? s->username : "");
    g_free(host);
}

static void
pass_prompt_leave(Client *c)
{
    (void)c;
    vb_floating_close();
    /* Cleanup happens in keypress on user decision. */
}

static VbResult
pass_prompt_keypress(Client *c, int key)
{
    PendingSave *s = g_hash_table_lookup(pending, c);
    if (!s) {
        vb_enter(c, 'n');
        return RESULT_COMPLETE;
    }

    if (key == 'y' || key == 'Y') {
        vb_pass_manager_save(manager,
                             s->origin, s->target_origin,
                             s->username, s->username,
                             s->password,
                             s->username_field, s->password_field,
                             s->is_new);
        vb_echo(c, MSG_NORMAL, TRUE, "Password saved.");
    } else if (key == 'n' || key == 'N') {
        char *host = host_from_uri(s->origin);
        if (host) g_hash_table_insert(deny_set, host, GINT_TO_POINTER(1));
        vb_echo(c, MSG_NORMAL, TRUE, "Not saved (session-scoped).");
    } else if (key == CTRL('[')) {
        vb_echo(c, MSG_NORMAL, TRUE, "");
    } else {
        /* consume any other keystroke silently */
        return RESULT_COMPLETE;
    }

    g_hash_table_remove(pending, c);
    vb_enter(c, 'n');
    return RESULT_COMPLETE;
}

/* ---------- UCM handlers --------------------------------------------- */

/* passwordManagerSave — silent save (Ephy sends this when a per-origin
 * PERMIT permission is set; we don't wire per-origin permissions in
 * phase 1, so this path is rarely hit, but honor it if it fires). */
static void
on_pass_save(WebKitUserContentManager *ucm, JSCValue *value, gpointer data)
{
    if (!enabled || !jsc_value_is_object(value)) return;

    const char *origin        = str_or_empty(value, "origin");
    if (!origin || !*origin) return;

    char *o  = g_strdup(str_or_empty(value, "origin"));
    char *to = g_strdup(str_or_empty(value, "targetOrigin"));
    char *u  = g_strdup(str_or_empty(value, "username"));
    char *p  = g_strdup(str_or_empty(value, "password"));
    char *uf = g_strdup(str_or_empty(value, "usernameField"));
    char *pf = g_strdup(str_or_empty(value, "passwordField"));

    vb_pass_manager_save(manager, o, to, u, u, p, uf, pf,
                         /*is_new=*/FALSE);

    g_free(o); g_free(to); g_free(u); g_free(p); g_free(uf); g_free(pf);
}

/* passwordManagerRequestSave — stash the record and pop the y/n prompt. */
static void
on_pass_request_save(WebKitUserContentManager *ucm, JSCValue *value, gpointer data)
{
    if (!enabled || !jsc_value_is_object(value)) return;
    Client *c = client_for_ucm(ucm);
    if (!c) return;

    const char *origin = str_or_empty(value, "origin");
    if (!origin || !*origin) return;

    char *host = host_from_uri(origin);
    if (is_denied(host)) { g_free(host); return; }
    g_free(host);

    PendingSave *s = g_new0(PendingSave, 1);
    s->origin         = g_strdup(str_or_empty(value, "origin"));
    s->target_origin  = g_strdup(str_or_empty(value, "targetOrigin"));
    s->username       = g_strdup(str_or_empty(value, "username"));
    s->password       = g_strdup(str_or_empty(value, "password"));
    s->username_field = g_strdup(str_or_empty(value, "usernameField"));
    s->password_field = g_strdup(str_or_empty(value, "passwordField"));
    {
        JSCValue *v = jsc_value_object_get_property(value, "isNew");
        s->is_new = v ? jsc_value_to_boolean(v) : FALSE;
        if (v) g_object_unref(v);
    }

    g_hash_table_replace(pending, c, s);
    vb_enter(c, 'P');
}

/* Query-password callback context — kept alive across the async
 * vb_pass_manager_query call. */
typedef struct {
    Client *client;
    int promise_id;
} QueryCtx;

static void
on_query_password_records(GList *records, gpointer user_data)
{
    QueryCtx *ctx = user_data;
    Client *c = ctx->client;

    /* Verify Client still exists (user may have closed the tab) */
    Client *check;
    for (check = vb.clients; check && check != c; check = check->next);

    if (check) {
        if (records) {
            VbPassRecord *r = records->data;
            reply_query_password(c, ctx->promise_id, r->username, r->password);
        } else {
            reply_query_password(c, ctx->promise_id, NULL, NULL);
        }
    }
    g_free(ctx);
}

/* PasswordManagerQueryPassword — async pass show, reply via eval. */
static void
on_pass_query(WebKitUserContentManager *ucm, JSCValue *value, gpointer data)
{
    if (!enabled || !jsc_value_is_object(value)) {
        return;
    }
    Client *c = client_for_ucm(ucm);
    if (!c) return;

    const char *origin        = g_strdup(str_or_empty(value, "origin"));
    const char *target_origin = g_strdup(str_or_empty(value, "targetOrigin"));
    const char *username      = g_strdup(str_or_empty(value, "username"));
    const char *u_field       = g_strdup(str_or_empty(value, "usernameField"));
    const char *p_field       = g_strdup(str_or_empty(value, "passwordField"));
    int promise_id;
    {
        JSCValue *v = jsc_value_object_get_property(value, "promiseID");
        promise_id = v ? (int)jsc_value_to_int32(v) : 0;
        if (v) g_object_unref(v);
    }

    QueryCtx *ctx = g_new0(QueryCtx, 1);
    ctx->client = c;
    ctx->promise_id = promise_id;

    vb_pass_manager_query(manager,
                          *origin ? origin : NULL,
                          *target_origin ? target_origin : NULL,
                          *username ? username : NULL,
                          *u_field ? u_field : NULL,
                          *p_field ? p_field : NULL,
                          on_query_password_records, ctx);

    g_free((char*)origin);
    g_free((char*)target_origin);
    g_free((char*)username);
    g_free((char*)u_field);
    g_free((char*)p_field);
}

/* PasswordManagerQueryUsernames — sync cache read. */
static void
on_pass_query_users(WebKitUserContentManager *ucm, JSCValue *value, gpointer data)
{
    if (!enabled || !jsc_value_is_object(value)) return;
    Client *c = client_for_ucm(ucm);
    if (!c) return;

    const char *origin = str_or_empty(value, "origin");
    int promise_id;
    {
        JSCValue *v = jsc_value_object_get_property(value, "promiseID");
        promise_id = v ? (int)jsc_value_to_int32(v) : 0;
        if (v) g_object_unref(v);
    }

    GList *users = vb_pass_manager_get_usernames_for_origin(manager, origin);
    reply_query_usernames(c, promise_id, users);
}

/* passwordFormFocused — insecure-action banner (Epiphany UI); we accept
 * and discard so ephy.js's FormManager focus listener has a live handler
 * to postMessage to. Silently swallowed for phase 1. */
static void
on_pass_form_focused(WebKitUserContentManager *ucm, JSCValue *value, gpointer data)
{
    (void)ucm; (void)value; (void)data;
}

/* ---------- init + registration -------------------------------------- */

void
vb_pass_ui_init(void)
{
    if (manager) return;
    manager = vb_pass_manager_new();
    deny_set = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    pending  = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                     NULL, pending_save_free);
    vb_mode_add('P', pass_prompt_enter, pass_prompt_leave,
                pass_prompt_keypress, NULL);
    vb_mode_add('F', fill_picker_enter, fill_picker_leave,
                fill_picker_keypress, fill_picker_input_changed);
}

void
vb_pass_ui_register_ucm(WebKitUserContentManager *ucm)
{
    webkit_user_content_manager_register_script_message_handler(ucm, "passwordManagerSave",         NULL);
    webkit_user_content_manager_register_script_message_handler(ucm, "passwordManagerRequestSave",  NULL);
    webkit_user_content_manager_register_script_message_handler(ucm, "passwordFormFocused",         NULL);
    webkit_user_content_manager_register_script_message_handler(ucm, "PasswordManagerQueryPassword",  NULL);
    webkit_user_content_manager_register_script_message_handler(ucm, "PasswordManagerQueryUsernames", NULL);

    g_signal_connect(ucm, "script-message-received::passwordManagerSave",
                     G_CALLBACK(on_pass_save),          NULL);
    g_signal_connect(ucm, "script-message-received::passwordManagerRequestSave",
                     G_CALLBACK(on_pass_request_save),  NULL);
    g_signal_connect(ucm, "script-message-received::passwordFormFocused",
                     G_CALLBACK(on_pass_form_focused),  NULL);
    g_signal_connect(ucm, "script-message-received::PasswordManagerQueryPassword",
                     G_CALLBACK(on_pass_query),         NULL);
    g_signal_connect(ucm, "script-message-received::PasswordManagerQueryUsernames",
                     G_CALLBACK(on_pass_query_users),   NULL);
}

/* ---------- :set pass-enabled ---------------------------------------- */

gboolean
vb_pass_ui_enabled(void)
{
    return enabled;
}

void
vb_pass_ui_set_enabled(gboolean v)
{
    enabled = v;
}

/* ---------- :pass-fill + :pass-forget -------------------------------- */

static char *
current_page_host(Client *c)
{
    if (!c || !c->webview) return NULL;
    const char *uri = webkit_web_view_get_uri(c->webview);
    return host_from_uri(uri);
}

static void
fill_from_record(Client *c, VbPassRecord *r)
{
    /* Prefer field-name selectors when known; fall back to first
     * matching input types. */
    char *ue = js_escape(r->username ? r->username : "");
    char *pe = js_escape(r->password ? r->password : "");
    char *ufe = js_escape(r->username_field ? r->username_field : "");
    char *pfe = js_escape(r->password_field ? r->password_field : "");

    /* Small IIFE — try to match by name/id, else by input type. Use
     * Ephy.autoFill if available for consistent dispatch semantics. */
    char *js = g_strdup_printf(
        "(function(){var fill=(el,v)=>{if(!el)return;"
        "if(window.Ephy&&Ephy.autoFill){Ephy.autoFill(el,v);}"
        "else{el.value=v;el.dispatchEvent(new Event('input',{bubbles:true}));"
        "el.dispatchEvent(new Event('change',{bubbles:true}));}};"
        "var u=null,p=null;"
        "var uf='%s',pf='%s';"
        "if(uf)u=document.querySelector('input[name=\"'+uf+'\"],input[id=\"'+uf+'\"]');"
        "if(pf)p=document.querySelector('input[name=\"'+pf+'\"],input[id=\"'+pf+'\"]');"
        "if(!p)p=document.querySelector('input[type=\"password\"]');"
        "if(!u&&p&&p.form){var els=Array.from(p.form.elements);"
        "for(var i=els.indexOf(p)-1;i>=0;i--){var e=els[i];"
        "if(e instanceof HTMLInputElement&&['text','email','tel','url','number'].includes(e.type)){u=e;break;}}}"
        "fill(u,'%s');fill(p,'%s');})();",
        ufe, pfe, ue, pe);
    ext_proxy_eval_script_in_page(c, js);

    g_free(ue); g_free(pe); g_free(ufe); g_free(pfe); g_free(js);
}

/* Fill-picker records-arrived callback. records is borrowed for the
 * duration of the call; we deep-copy what we need. */
static void
on_fill_records(GList *records, gpointer user_data)
{
    Client *c = user_data;

    /* Verify c still alive */
    Client *check;
    for (check = vb.clients; check && check != c; check = check->next);
    if (!check) return;

    guint n = g_list_length(records);
    if (n == 0) {
        vb_echo(c, MSG_ERROR, TRUE, "pass-fill: no matches for this site");
        return;
    }

    if (n == 1) {
        fill_from_record(c, records->data);
        vb_echo(c, MSG_NORMAL, TRUE, "Filled from pass.");
        return;
    }

    /* Multiple candidates — build a numbered menu and stash a copy of
     * the records for the picker mode. */
    fill_ctx_reset();
    fill_ctx = g_new0(FillCtx, 1);
    fill_ctx->client = c;
    for (GList *l = records; l; l = l->next)
        fill_ctx->records = g_list_append(fill_ctx->records,
                                          vb_pass_record_copy(l->data));

    vb_enter(c, 'F');
}

VbCmdResult
vb_pass_ui_fill(Client *c, const char *host_arg)
{
    if (!c) return CMD_ERROR;

    /* host_arg may include a "/username" suffix for direct selection.
     * For phase 1: parse "host/user" if slash present; else treat as
     * host only. */
    char *host  = NULL;
    char *user  = NULL;
    if (host_arg && *host_arg) {
        const char *slash = strchr(host_arg, '/');
        if (slash) {
            host = g_strndup(host_arg, slash - host_arg);
            user = g_strdup(slash + 1);
        } else {
            host = g_strdup(host_arg);
        }
    } else {
        host = current_page_host(c);
    }
    if (!host) {
        vb_echo(c, MSG_ERROR, TRUE, "pass-fill: no host");
        return CMD_ERROR;
    }

    /* Build a fake origin (scheme+host) for query — since the manager
     * matches on host, https:// is a safe assumption for a lookup key. */
    char *origin = g_strdup_printf("https://%s", host);

    vb_pass_manager_query(manager, origin, NULL, user, NULL, NULL,
                          on_fill_records, c);

    g_free(host);
    g_free(user);
    g_free(origin);
    return CMD_SUCCESS;
}

/* Fill-picker mode 'F' handlers. */
static void
fill_picker_enter(Client *c)
{
    vb_modelabel_update(c, "-- PASS PICK --");
    vb_floating_open("Fill password");
    if (fill_ctx && fill_ctx->client == c) {
        fill_picker_build_widgets();
    }
    /* Empty the inputbox so the user starts with a clean filter, then
     * grab focus so typing accumulates as the needle (buffer-changed
     * fires fill_picker_input_changed). vb_input_set_text hides the
     * inputbox when input-autohide is on and text is empty, so force
     * visibility back — an invisible widget cannot take focus. */
    vb_input_set_text(c, "");
    gtk_widget_set_visible(GTK_WIDGET(c->input), TRUE);
    vb_floating_grab_focus();
}

static void
fill_picker_leave(Client *c)
{
    (void)c;
    vb_floating_close();
}

static VbResult
fill_picker_keypress(Client *c, int key)
{
    if (!fill_ctx || fill_ctx->client != c) {
        vb_enter(c, 'n');
        return RESULT_COMPLETE;
    }

    if (key == CTRL('[')) {
        fill_ctx_reset();
        vb_input_set_text(c, "");
        vb_echo(c, MSG_NORMAL, TRUE, "");
        vb_enter(c, 'n');
        return RESULT_COMPLETE;
    }

    GListModel *m = fill_ctx->filter_model ? G_LIST_MODEL(fill_ctx->filter_model) : NULL;
    guint n = m ? g_list_model_get_n_items(m) : 0;

    if (key == KEY_UP || key == KEY_DOWN) {
        if (n == 0) return RESULT_COMPLETE;
        guint pos = gtk_single_selection_get_selected(fill_ctx->selection);
        if (pos == GTK_INVALID_LIST_POSITION) pos = 0;
        else if (key == KEY_UP)   pos = (pos == 0) ? n - 1 : pos - 1;
        else                      pos = (pos + 1) % n;
        gtk_single_selection_set_selected(fill_ctx->selection, pos);
        gtk_list_view_scroll_to(GTK_LIST_VIEW(fill_ctx->listview),
                                pos, GTK_LIST_SCROLL_NONE, NULL);
        return RESULT_COMPLETE;
    }

    if (key == KEY_CR) {
        if (n == 0) {
            vb_echo(c, MSG_ERROR, TRUE, "pass-fill: no match");
            fill_ctx_reset();
            vb_input_set_text(c, "");
            vb_enter(c, 'n');
            return RESULT_COMPLETE;
        }
        guint pos = gtk_single_selection_get_selected(fill_ctx->selection);
        if (pos == GTK_INVALID_LIST_POSITION) pos = 0;
        GtkStringObject *so = g_list_model_get_item(m, pos);
        const char *pick    = so ? gtk_string_object_get_string(so) : NULL;
        /* Map the displayed username string back to its record. Duplicates
         * are impossible — pass hierarchy uses filename-as-username so
         * each row is unique per host. */
        VbPassRecord *hit = NULL;
        for (GList *l = fill_ctx->records; l && pick; l = l->next) {
            VbPassRecord *r = l->data;
            const char *u = r->username && *r->username ? r->username : "(no-user)";
            if (strcmp(u, pick) == 0) { hit = r; break; }
        }
        if (hit) {
            fill_from_record(c, hit);
            vb_echo(c, MSG_NORMAL, TRUE, "Filled from pass.");
        } else {
            vb_echo(c, MSG_ERROR, TRUE, "pass-fill: selection lookup failed");
        }
        g_clear_object(&so);
        fill_ctx_reset();
        vb_input_set_text(c, "");
        vb_enter(c, 'n');
        return RESULT_COMPLETE;
    }

    return RESULT_COMPLETE;  /* consume everything else — printable chars
                              * come in via IM through the buffer-changed path */
}

static void
fill_picker_input_changed(Client *c, const char *text)
{
    if (!fill_ctx || fill_ctx->client != c || !fill_ctx->filter) return;
    g_string_assign(fill_ctx->needle, text ? text : "");
    gtk_filter_changed(GTK_FILTER(fill_ctx->filter), GTK_FILTER_CHANGE_DIFFERENT);
    /* Re-anchor selection on the first surviving row so Enter picks it. */
    if (gtk_single_selection_get_selected(fill_ctx->selection) == GTK_INVALID_LIST_POSITION
        && g_list_model_get_n_items(G_LIST_MODEL(fill_ctx->filter_model)) > 0) {
        gtk_single_selection_set_selected(fill_ctx->selection, 0);
    }
}

/* :pass-forget — no arg = forget for current host all users; else host or host/user. */
static void
on_forget_done(GObject *src, GAsyncResult *res, gpointer user_data)
{
    Client *c = user_data;
    GError *err = NULL;
    if (vb_pass_manager_forget_finish(manager, res, &err)) {
        vb_echo(c, MSG_NORMAL, TRUE, "Forgotten.");
    } else {
        vb_echo(c, MSG_ERROR, TRUE, "pass-forget: %s",
                err ? err->message : "failed");
        g_clear_error(&err);
    }
}

VbCmdResult
vb_pass_ui_forget(Client *c, const char *host_arg)
{
    if (!c) return CMD_ERROR;

    char *host = NULL;
    char *user = NULL;
    if (host_arg && *host_arg) {
        const char *slash = strchr(host_arg, '/');
        if (slash) {
            host = g_strndup(host_arg, slash - host_arg);
            user = g_strdup(slash + 1);
        } else {
            host = g_strdup(host_arg);
        }
    } else {
        host = current_page_host(c);
    }
    if (!host) {
        vb_echo(c, MSG_ERROR, TRUE, "pass-forget: no host");
        return CMD_ERROR;
    }

    if (!user) {
        vb_echo(c, MSG_ERROR, TRUE,
                "pass-forget: specify host/user; forgetting all is not supported yet");
        g_free(host);
        return CMD_ERROR;
    }

    char *key = g_strdup_printf("websites/%s/%s", host, user);
    vb_pass_manager_forget(manager, key, NULL, on_forget_done, c);
    g_free(key);
    g_free(host);
    g_free(user);
    return CMD_SUCCESS;
}

