/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2026 Craig Miller
 *
 * pass(1)-backed credential storage. Serves as the model layer for the
 * password manager; see pass-ui.c for the controller and Client wiring.
 *
 * Record schema:
 *   ~/.password-store/websites/<host>/<username>.gpg
 * Body format (browserpass/PassFF compatible):
 *   <password>
 *   username: <u>
 *   username_field: <f>
 *   password_field: <f>
 *   origin: <scheme>://<host>[:port]
 *   target_origin: <scheme>://<host>[:port]
 *   time_password_changed: <ms since epoch>
 *
 * Null-username case (Google multi-step first save) uses "_" as the
 * filename stem and stores an empty username field.
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

#include <gio/gio.h>
#include <string.h>

#include "pass-manager.h"

#define NULL_USERNAME_SLUG "_"
#define STORE_SUBDIR       "websites"

struct passmanager {
    char        *store_root;   /* $PASSWORD_STORE_DIR or ~/.password-store */
    char        *websites_dir; /* store_root + "/websites" */
    GHashTable  *cache;        /* host (char*) -> GList<char* slug> */
};

/* ---------- record helpers ---------- */

VbPassRecord *
vb_pass_record_new(void)
{
    return g_new0(VbPassRecord, 1);
}

VbPassRecord *
vb_pass_record_copy(const VbPassRecord *r)
{
    VbPassRecord *c = vb_pass_record_new();
    c->origin                 = g_strdup(r->origin);
    c->target_origin          = g_strdup(r->target_origin);
    c->username               = g_strdup(r->username);
    c->password               = g_strdup(r->password);
    c->username_field         = g_strdup(r->username_field);
    c->password_field         = g_strdup(r->password_field);
    c->time_password_changed  = r->time_password_changed;
    return c;
}

void
vb_pass_record_free(VbPassRecord *r)
{
    if (!r) return;
    g_free(r->origin);
    g_free(r->target_origin);
    g_free(r->username);
    g_free(r->password);
    g_free(r->username_field);
    g_free(r->password_field);
    g_free(r);
}

/* ---------- static helpers ---------- */

static char *
host_from_origin(const char *origin)
{
    if (!origin || !*origin) return NULL;
    GUri *uri = g_uri_parse(origin, G_URI_FLAGS_NONE, NULL);
    if (!uri) return NULL;
    const char *host = g_uri_get_host(uri);
    char *out = (host && *host) ? g_strdup(host) : NULL;
    g_uri_unref(uri);
    return out;
}

static char *
slug_for_username(const char *username)
{
    if (!username || !*username) return g_strdup(NULL_USERNAME_SLUG);
    return g_strdup(username);
}

/* Build the pass-store-relative path, e.g. "websites/kagi.com/craig". */
static char *
pass_key(const char *host, const char *username)
{
    char *slug = slug_for_username(username);
    char *key  = g_build_filename(STORE_SUBDIR, host, slug, NULL);
    g_free(slug);
    return key;
}

/* Serialize record body for `pass insert -m` stdin. */
static char *
format_body(const VbPassRecord *r)
{
    GString *s = g_string_sized_new(256);
    g_string_append(s, r->password ? r->password : "");
    g_string_append_c(s, '\n');
    if (r->username && *r->username)
        g_string_append_printf(s, "username: %s\n", r->username);
    if (r->username_field && *r->username_field)
        g_string_append_printf(s, "username_field: %s\n", r->username_field);
    if (r->password_field && *r->password_field)
        g_string_append_printf(s, "password_field: %s\n", r->password_field);
    if (r->origin && *r->origin)
        g_string_append_printf(s, "origin: %s\n", r->origin);
    if (r->target_origin && *r->target_origin)
        g_string_append_printf(s, "target_origin: %s\n", r->target_origin);
    if (r->time_password_changed)
        g_string_append_printf(s, "time_password_changed: %" G_GINT64_FORMAT "\n",
                               r->time_password_changed);
    return g_string_free(s, FALSE);
}

/* Parse `pass show` output into a fresh VbPassRecord. Returns NULL on empty. */
static VbPassRecord *
parse_body(const char *text)
{
    if (!text || !*text) return NULL;
    VbPassRecord *r = vb_pass_record_new();
    char **lines = g_strsplit(text, "\n", -1);
    if (lines[0]) r->password = g_strdup(lines[0]);
    for (int i = 1; lines[i]; i++) {
        const char *line = lines[i];
        const char *colon = strchr(line, ':');
        if (!colon) continue;
        gsize klen = colon - line;
        const char *val = colon + 1;
        while (*val == ' ') val++;
        if (!*val) continue;

#define MATCH(k) (klen == strlen(k) && strncmp(line, k, klen) == 0)
        if      (MATCH("username"))              r->username        = g_strdup(val);
        else if (MATCH("username_field"))        r->username_field  = g_strdup(val);
        else if (MATCH("password_field"))        r->password_field  = g_strdup(val);
        else if (MATCH("origin"))                r->origin          = g_strdup(val);
        else if (MATCH("target_origin"))         r->target_origin   = g_strdup(val);
        else if (MATCH("time_password_changed")) r->time_password_changed = g_ascii_strtoll(val, NULL, 10);
#undef MATCH
    }
    g_strfreev(lines);
    return r;
}

/* ---------- subprocess wrappers ---------- */

/* Sync `pass show <key>` → allocated stdout. NULL on failure. */
static char *
run_pass_show(const char *key, GError **error)
{
    GSubprocess *proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        error, "pass", "show", key, NULL);
    if (!proc) return NULL;

    char *out = NULL;
    if (!g_subprocess_communicate_utf8(proc, NULL, NULL, &out, NULL, error)) {
        g_object_unref(proc);
        g_free(out);
        return NULL;
    }
    if (!g_subprocess_get_successful(proc)) {
        g_object_unref(proc);
        g_free(out);
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                    "pass show %s failed", key);
        return NULL;
    }
    g_object_unref(proc);
    return out;
}

/* Sync `pass insert -m [-f] <key>` with body on stdin. */
static gboolean
run_pass_insert(const char *key, const char *body, gboolean force, GError **error)
{
    const char *argv_plain[] = {"pass", "insert", "-m", "--", key, NULL};
    const char *argv_force[] = {"pass", "insert", "-m", "-f", "--", key, NULL};
    GSubprocess *proc = g_subprocess_newv(
        (const gchar * const *)(force ? argv_force : argv_plain),
        G_SUBPROCESS_FLAGS_STDIN_PIPE
            | G_SUBPROCESS_FLAGS_STDOUT_SILENCE
            | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        error);
    if (!proc) return FALSE;

    gboolean ok = g_subprocess_communicate_utf8(proc, body, NULL, NULL, NULL, error)
                  && g_subprocess_get_successful(proc);
    if (!ok && error && !*error)
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                    "pass insert %s failed", key);
    g_object_unref(proc);
    return ok;
}

/* Sync `pass rm -f <key>`. */
static gboolean
run_pass_rm(const char *key, GCancellable *cancellable, GError **error)
{
    GSubprocess *proc = g_subprocess_new(
        G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE,
        error, "pass", "rm", "-f", key, NULL);
    if (!proc) return FALSE;
    gboolean ok = g_subprocess_wait_check(proc, cancellable, error);
    g_object_unref(proc);
    return ok;
}

/* ---------- cache ---------- */

static void
host_slugs_free(gpointer p)
{
    g_list_free_full((GList*)p, g_free);
}

/* Rebuild cache entry for one host by scanning its directory. */
static void
refresh_host_cache(VbPassManager *self, const char *host)
{
    char *host_dir = g_build_filename(self->websites_dir, host, NULL);
    GDir *d = g_dir_open(host_dir, 0, NULL);
    g_free(host_dir);

    GList *slugs = NULL;
    if (d) {
        const char *name;
        while ((name = g_dir_read_name(d))) {
            if (!g_str_has_suffix(name, ".gpg")) continue;
            gsize len = strlen(name) - 4;
            slugs = g_list_prepend(slugs, g_strndup(name, len));
        }
        g_dir_close(d);
    }

    if (slugs)
        g_hash_table_insert(self->cache, g_strdup(host), g_list_reverse(slugs));
    else
        g_hash_table_remove(self->cache, host);
}

static void
warm_cache(VbPassManager *self)
{
    GDir *d = g_dir_open(self->websites_dir, 0, NULL);
    if (!d) return;
    const char *host;
    while ((host = g_dir_read_name(d)))
        refresh_host_cache(self, host);
    g_dir_close(d);
}

/* ---------- construction / destruction ---------- */

VbPassManager *
vb_pass_manager_new(void)
{
    VbPassManager *self = g_new0(VbPassManager, 1);

    const char *store_env = g_getenv("PASSWORD_STORE_DIR");
    self->store_root = store_env
        ? g_strdup(store_env)
        : g_build_filename(g_get_home_dir(), ".password-store", NULL);
    self->websites_dir = g_build_filename(self->store_root, STORE_SUBDIR, NULL);
    self->cache = g_hash_table_new_full(g_str_hash, g_str_equal,
                                        g_free, host_slugs_free);
    warm_cache(self);
    return self;
}

void
vb_pass_manager_free(VbPassManager *self)
{
    if (!self) return;
    g_hash_table_destroy(self->cache);
    g_free(self->store_root);
    g_free(self->websites_dir);
    g_free(self);
}

/* ---------- sync API ---------- */

GList *
vb_pass_manager_get_usernames_for_origin(VbPassManager *self, const char *origin)
{
    char *host = host_from_origin(origin);
    if (!host) return NULL;
    GList *list = g_hash_table_lookup(self->cache, host);
    g_free(host);
    return list;
}

gboolean
vb_pass_manager_find(VbPassManager *self, const char *origin, const char *username)
{
    char *host = host_from_origin(origin);
    if (!host) return FALSE;
    GList *list = g_hash_table_lookup(self->cache, host);
    g_free(host);

    char *slug = slug_for_username(username);
    for (GList *l = list; l; l = l->next) {
        if (g_strcmp0(l->data, slug) == 0) { g_free(slug); return TRUE; }
    }
    g_free(slug);
    return FALSE;
}

/* ---------- save (sync, invoked from main thread) ---------- */

void
vb_pass_manager_save(VbPassManager *self,
                     const char *origin,
                     const char *target_origin,
                     const char *username,
                     const char *new_username,
                     const char *password,
                     const char *username_field,
                     const char *password_field,
                     gboolean is_new)
{
    if (!origin || !password) return;
    char *host = host_from_origin(origin);
    if (!host) return;

    const char *save_user = new_username ? new_username : username;

    VbPassRecord r = {0};
    r.origin                = (char*)origin;
    r.target_origin         = (char*)(target_origin ? target_origin : origin);
    r.username              = (char*)save_user;
    r.password              = (char*)password;
    r.username_field        = (char*)username_field;
    r.password_field        = (char*)password_field;
    r.time_password_changed = g_get_real_time() / 1000;  /* µs → ms */

    char *body = format_body(&r);
    char *key  = pass_key(host, save_user);
    GError *err = NULL;

    /* Rename case: username changed. Delete old, then insert new. */
    if (!is_new && new_username && username && g_strcmp0(username, new_username) != 0) {
        char *old_key = pass_key(host, username);
        run_pass_rm(old_key, NULL, NULL);
        g_free(old_key);
    }

    /* is_new=FALSE means overwrite; is_new=TRUE means fail-if-exists. */
    if (!run_pass_insert(key, body, /*force=*/!is_new, &err)) {
        g_warning("vb_pass_manager_save: %s: %s",
                  key, err ? err->message : "unknown");
        g_clear_error(&err);
    } else {
        refresh_host_cache(self, host);
    }

    g_free(body);
    g_free(key);
    g_free(host);
}

/* ---------- async query ---------- */

typedef struct {
    char *host;             /* snapshot on main thread — worker never touches cache */
    GList *slugs;           /* deep-copy of cache entry; owned by us */
    char *origin;
    char *target_origin;
    char *username;
    char *username_field;
    char *password_field;
    VbPassQueryCallback cb;
    gpointer user_data;
    GList *records;         /* accumulated in the worker; consumed on main */
} QueryData;

static void
query_data_free(gpointer p)
{
    QueryData *d = p;
    g_free(d->host);
    g_list_free_full(d->slugs, g_free);
    g_free(d->origin);
    g_free(d->target_origin);
    g_free(d->username);
    g_free(d->username_field);
    g_free(d->password_field);
    g_list_free_full(d->records, (GDestroyNotify)vb_pass_record_free);
    g_free(d);
}

static gboolean
str_filter_matches(const char *filter, const char *value)
{
    /* NULL filter = wildcard; NULL value on a non-NULL filter = no match. */
    if (!filter) return TRUE;
    if (!value)  return FALSE;
    return strcmp(filter, value) == 0;
}

static void
query_thread(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
    QueryData *d = task_data;

    for (GList *l = d->slugs; l; l = l->next) {
        const char *slug = l->data;
        gboolean slug_is_null_user = (g_strcmp0(slug, NULL_USERNAME_SLUG) == 0);

        /* Username filter shortcut */
        if (d->username) {
            if (slug_is_null_user)                     continue;
            if (strcmp(slug, d->username) != 0)        continue;
        }

        char *key = pass_key(d->host, slug_is_null_user ? NULL : slug);
        GError *err = NULL;
        char *body = run_pass_show(key, &err);
        g_free(key);

        if (!body) {
            if (err) g_clear_error(&err);
            continue;
        }
        VbPassRecord *r = parse_body(body);
        g_free(body);
        if (!r) continue;

        /* Remaining filters — target/field-name matching. */
        if (!str_filter_matches(d->target_origin,  r->target_origin)  ||
            !str_filter_matches(d->username_field, r->username_field) ||
            !str_filter_matches(d->password_field, r->password_field)) {
            vb_pass_record_free(r);
            continue;
        }
        d->records = g_list_prepend(d->records, r);
    }

    d->records = g_list_reverse(d->records);
    g_task_return_pointer(task, NULL, NULL);
}

static void
query_done(GObject *source, GAsyncResult *result, gpointer user_data)
{
    QueryData *d = user_data;
    d->cb(d->records, d->user_data);
    /* records freed by query_data_free via task_data destructor */
}

void
vb_pass_manager_query(VbPassManager *self,
                      const char *origin,
                      const char *target_origin,
                      const char *username,
                      const char *username_field,
                      const char *password_field,
                      VbPassQueryCallback callback,
                      gpointer user_data)
{
    QueryData *d = g_new0(QueryData, 1);
    d->host            = host_from_origin(origin);
    d->origin          = g_strdup(origin);
    d->target_origin   = g_strdup(target_origin);
    d->username        = g_strdup(username);
    d->username_field  = g_strdup(username_field);
    d->password_field  = g_strdup(password_field);
    d->cb              = callback;
    d->user_data       = user_data;

    /* Snapshot the slug list on the main thread — cache is not thread-safe,
     * and the worker must not touch self->cache. */
    if (d->host) {
        GList *live = g_hash_table_lookup(self->cache, d->host);
        for (GList *l = live; l; l = l->next)
            d->slugs = g_list_prepend(d->slugs, g_strdup(l->data));
        d->slugs = g_list_reverse(d->slugs);
    }

    GTask *task = g_task_new(NULL, NULL, query_done, d);
    g_task_set_task_data(task, d, query_data_free);
    g_task_run_in_thread(task, query_thread);
    g_object_unref(task);
}

/* ---------- async forget ---------- */

typedef struct { VbPassManager *self; char *key; } ForgetData;

static void
forget_data_free(gpointer p)
{
    ForgetData *d = p;
    g_free(d->key);
    g_free(d);
}

static void
forget_thread(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable)
{
    ForgetData *d = task_data;
    GError *err = NULL;
    if (!run_pass_rm(d->key, cancellable, &err)) {
        g_task_return_error(task, err);
        return;
    }
    g_task_return_boolean(task, TRUE);
}

void
vb_pass_manager_forget(VbPassManager *self,
                       const char *key,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    ForgetData *d = g_new0(ForgetData, 1);
    d->self = self;
    d->key  = g_strdup(key);
    GTask *task = g_task_new(self, cancellable, callback, user_data);
    g_task_set_task_data(task, d, forget_data_free);
    g_task_run_in_thread(task, forget_thread);
    g_object_unref(task);
}

gboolean
vb_pass_manager_forget_finish(VbPassManager *self,
                              GAsyncResult *result,
                              GError **error)
{
    ForgetData *d = g_task_get_task_data(G_TASK(result));
    gboolean ok = g_task_propagate_boolean(G_TASK(result), error);
    if (ok && d) {
        /* key is "websites/<host>/<slug>" — refresh that host's cache. */
        char **parts = g_strsplit(d->key, "/", 3);
        if (g_strv_length(parts) >= 2)
            refresh_host_cache(self, parts[1]);
        g_strfreev(parts);
    }
    return ok;
}
