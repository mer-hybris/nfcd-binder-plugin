/*
 * Copyright (C) 2025 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "binder_nfc_watcher.h"

#include <gbinder.h>

#include <gutil_misc.h>

typedef GObjectClass BinderNfcWatcherObjectClass;
typedef struct binder_nfc_watcher_object {
    BinderNfcWatcher pub;
    GHashTable* services;
    GBinderServiceManager* sm;
    gulong name_watch_id;
    gulong list_call_id;
} BinderNfcWatcherObject;

typedef struct binder_nfc_watcher_service_entry {
    BinderNfcWatcherObject* obj;
    GBinderRemoteObject* remote;
    char* fqname;
    gulong get_id;
    gulong death_id;
} BinderNfcWatcherServiceEntry;

#define PARENT_CLASS binder_nfc_watcher_object_parent_class
#define PARENT_TYPE G_TYPE_OBJECT
#define THIS_TYPE binder_nfc_watcher_object_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, \
    BinderNfcWatcherObject)

GType THIS_TYPE G_GNUC_INTERNAL;
G_DEFINE_TYPE(BinderNfcWatcherObject, binder_nfc_watcher_object, PARENT_TYPE)

enum binder_nfc_watcher_signal {
    SIGNAL_SERVICE_FOUND,
    SIGNAL_COUNT
};

#define SIGNAL_SERVICE_FOUND_NAME   "binder-nfc-watcher-service-found"

static guint binder_nfc_watcher_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
binder_nfc_watcher_service_entry_free(
    gpointer user_data)
{
    BinderNfcWatcherServiceEntry* entry = user_data;
    BinderNfcWatcherObject* obj = entry->obj;

    gbinder_servicemanager_cancel(obj->sm, entry->get_id);
    gbinder_remote_object_remove_handler(entry->remote, entry->death_id);
    gbinder_remote_object_unref(entry->remote);
    g_free(entry->fqname);
    g_free(entry);
}

static
void
binder_nfc_watcher_service_dead(
    GBinderRemoteObject* remote,
    void* user_data)
{
    BinderNfcWatcherServiceEntry* entry = user_data;
    BinderNfcWatcherObject* obj = entry->obj;

    GWARN("Service %s (%s) has died", entry->fqname, obj->pub.backend->name);
    gbinder_remote_object_remove_handler(entry->remote, entry->death_id);
    entry->death_id = 0;
    g_hash_table_remove(obj->services, entry->fqname);
}

static
void
binder_nfc_watcher_service_get_reply(
    GBinderServiceManager* sm,
    GBinderRemoteObject* remote,
    int status,
    void* user_data)
{
    BinderNfcWatcherServiceEntry* entry = user_data;

    entry->get_id = 0;
    GASSERT(!entry->remote);
    if (remote) {
        GDEBUG("Connected to %s", entry->fqname);
        entry->remote = gbinder_remote_object_ref(remote);
        entry->death_id = gbinder_remote_object_add_death_handler(remote,
            binder_nfc_watcher_service_dead, entry);
        g_signal_emit(entry->obj, binder_nfc_watcher_signals
            [SIGNAL_SERVICE_FOUND], 0, remote, entry->fqname);
    } else {
        GWARN("Couldn't get remote handle to %s", entry->fqname);
    }
}

static
void
binder_nfc_watcher_service_found(
    BinderNfcWatcherObject* self,
    const char* fqname)
{
    if (!g_hash_table_contains(self->services, fqname)) {
        BinderNfcWatcherServiceEntry* entry =
            g_new0(BinderNfcWatcherServiceEntry, 1);

        GDEBUG("Found %s (%s)", fqname, self->pub.backend->name);
        entry->obj = self;
        entry->fqname = g_strdup(fqname);
        g_hash_table_insert(self->services, entry->fqname, entry);
        entry->get_id = gbinder_servicemanager_get_service(self->sm, fqname,
            binder_nfc_watcher_service_get_reply, entry);
    }
}

static
gboolean
binder_nfc_watcher_service_list_proc(
    GBinderServiceManager* sm,
    char** services,
    void* watcher)
{
    BinderNfcWatcherObject* self = THIS(watcher);

    self->list_call_id = 0;
    if (services) {
        const BinderNfcBackend* backend = self->pub.backend;
        const gsize prefix_len = strlen(backend->watch);
        char** ptr;

        for (ptr = services; *ptr; ptr++) {
            const char* fqname = *ptr;

            /*
             * Accept either the full match or a partial match with a suffix
             * beginning with a slash.
             */
            if (g_str_has_prefix(fqname, backend->watch) &&
               (!fqname[prefix_len] || fqname[prefix_len] == '/')) {
                binder_nfc_watcher_service_found(self, fqname);
            }
        }
    }
    return FALSE; /* Free the service list */
}

static
void
binder_nfc_watcher_registration_handler(
    GBinderServiceManager* sm,
    const char* name,
    void* watcher)
{
    BinderNfcWatcherObject* self = THIS(watcher);

    gbinder_servicemanager_cancel(sm, self->list_call_id);
    self->list_call_id = gbinder_servicemanager_list(sm,
        binder_nfc_watcher_service_list_proc, self);
}

/*==========================================================================*
 * API
 *==========================================================================*/

BinderNfcWatcher*
binder_nfc_watcher_new(
    const BinderNfcBackend* backend)
{
    GBinderServiceManager* sm = gbinder_servicemanager_new(backend->dev);

    if (sm) {
        BinderNfcWatcherObject* self = g_object_new(THIS_TYPE, NULL);
        BinderNfcWatcher* watcher = &self->pub;

        watcher->backend = backend;
        self->sm = sm;
        self->list_call_id = gbinder_servicemanager_list
            (sm, binder_nfc_watcher_service_list_proc, self);
        self->name_watch_id = gbinder_servicemanager_add_registration_handler
            (sm, backend->watch, binder_nfc_watcher_registration_handler, self);
        return watcher;
    }
    return NULL;
}

gulong
binder_nfc_watcher_add_handler(
    BinderNfcWatcher* watcher,
    BinderNfcWatcherFunc func,
    void* user_data)
{
    return (G_LIKELY(watcher) && G_LIKELY(func)) ?
        g_signal_connect(THIS(watcher), SIGNAL_SERVICE_FOUND_NAME,
        G_CALLBACK(func), user_data) : 0;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_watcher_object_init(
    BinderNfcWatcherObject* self)
{
    self->services = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, binder_nfc_watcher_service_entry_free);
}

static
void
binder_nfc_watcher_object_finalize(
    GObject* object)
{
    BinderNfcWatcherObject* self = THIS(object);

    g_hash_table_destroy(self->services);
    gbinder_servicemanager_remove_handler(self->sm, self->name_watch_id);
    gbinder_servicemanager_cancel(self->sm, self->list_call_id);
    gbinder_servicemanager_unref(self->sm);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
binder_nfc_watcher_object_class_init(
    BinderNfcWatcherObjectClass* klass)
{
    klass->finalize = binder_nfc_watcher_object_finalize;
    binder_nfc_watcher_signals[SIGNAL_SERVICE_FOUND] =
        g_signal_new(SIGNAL_SERVICE_FOUND_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_STRING);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
