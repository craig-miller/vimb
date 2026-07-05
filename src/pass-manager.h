/**
 * vimb - a webkit based vim like browser.
 *
 * Copyright (C) 2026 Craig Miller
 * Interface shape borrowed from GNOME Web (Epiphany) EphyPasswordManager.
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

#ifndef _PASS_MANAGER_H
#define _PASS_MANAGER_H

#include <gio/gio.h>

typedef struct passmanager VbPassManager;

typedef struct {
    char   *origin;                 /* scheme+host[:port] of page */
    char   *target_origin;          /* scheme+host[:port] of form action */
    char   *username;               /* nullable (Google multi-step first save) */
    char   *password;
    char   *username_field;         /* nullable form field name/id */
    char   *password_field;
    gint64  time_password_changed;  /* ms since epoch */
} VbPassRecord;

/* Async query callback. records is a GList<VbPassRecord*> borrowed for the
 * duration of the callback; the manager frees each record afterwards. Callers
 * who need to retain must copy (vb_pass_record_copy) or steal fields. */
typedef void (*VbPassQueryCallback)(GList *records, gpointer user_data);

VbPassManager *vb_pass_manager_new(void);
void           vb_pass_manager_free(VbPassManager *self);

/* Async — spawns `pass show` per candidate. Any NULL filter is a wildcard.
 * Callback runs on the main context. */
void vb_pass_manager_query(VbPassManager *self,
                           const char *origin,
                           const char *target_origin,
                           const char *username,
                           const char *username_field,
                           const char *password_field,
                           VbPassQueryCallback callback,
                           gpointer user_data);

/* Sync — cache-backed. Borrowed list of char*; do not free. Empty list if
 * no matches. Used by the JS-side picker to enumerate before decrypting. */
GList *vb_pass_manager_get_usernames_for_origin(VbPassManager *self,
                                                const char *origin);

/* Upsert.
 *   is_new=TRUE  — insert a fresh record (fails silently if collision).
 *   is_new=FALSE — locate by (origin, target_origin, username, username_field,
 *                  password_field) and rewrite. If new_username differs from
 *                  username, delete the old path and insert at the new one.
 * password must be non-NULL. Fire-and-forget; failures logged to stderr. */
void vb_pass_manager_save(VbPassManager *self,
                          const char *origin,
                          const char *target_origin,
                          const char *username,
                          const char *new_username,
                          const char *password,
                          const char *username_field,
                          const char *password_field,
                          gboolean is_new);

/* Delete by pass-path key relative to the store root, e.g.
 * "websites/kagi.com/craig". */
void     vb_pass_manager_forget(VbPassManager *self,
                                const char *key,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);
gboolean vb_pass_manager_forget_finish(VbPassManager *self,
                                       GAsyncResult *result,
                                       GError **error);

/* Sync existence probe (cache walk, no decryption). */
gboolean vb_pass_manager_find(VbPassManager *self,
                              const char *origin,
                              const char *username);

/* Record helpers */
VbPassRecord *vb_pass_record_new(void);
VbPassRecord *vb_pass_record_copy(const VbPassRecord *r);
void          vb_pass_record_free(VbPassRecord *r);

#endif /* end of include guard: _PASS_MANAGER_H */
