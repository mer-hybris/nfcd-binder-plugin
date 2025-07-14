/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
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

#include "binder_nfc_adapter.h"
#include "binder_nfc_api_aidl.h"
#include "binder_nfc_api_hidl.h"
#include "binder_nfc_watcher.h"
#include "plugin.h"

#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_plugin_impl.h>

#include <nci_types.h>

#include <gbinder.h>

#include <gutil_misc.h>

GLOG_MODULE_DEFINE("binder");

typedef struct binder_nfc_plugin_adapter_entry {
    gulong death_id;
    NfcAdapter* adapter;
} BinderNfcPluginEntry;

typedef struct binder_nfc_plugin_start_watch_entry {
    gulong watch_id;
    BinderNfcWatcher* watcher;
} BinderNfcStartWatchEntry;

typedef NfcPluginClass BinderNfcPluginClass;
typedef struct binder_nfc_plugin {
    NfcPlugin parent;
    NfcManager* manager;
    GHashTable* adapters;
    GSList* start_watches;
    BinderNfcWatcher* watcher;
    gulong watch_id;
} BinderNfcPlugin;

#define PARENT_CLASS binder_nfc_plugin_parent_class
#define PARENT_TYPE NFC_TYPE_PLUGIN
#define THIS_TYPE binder_nfc_plugin_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj,THIS_TYPE,BinderNfcPlugin)

GType THIS_TYPE G_GNUC_INTERNAL;
G_DEFINE_TYPE(BinderNfcPlugin, binder_nfc_plugin, PARENT_TYPE)

/*==========================================================================*
 * Binder APIs
 *==========================================================================*/

static const BinderNfcBackend binder_nfc_backends[] = {
    {
        "hidl",
        GBINDER_DEFAULT_HWBINDER,
        BINDER_NFC_HIDL_IFACE,
        binder_nfc_api_hidl_new
    },{
        "aidl",
        GBINDER_DEFAULT_BINDER,
        BINDER_NFC_AIDL_IFACE "/default",
        binder_nfc_api_aidl_new
    }
};

#define N_BACKENDS G_N_ELEMENTS(binder_nfc_backends)

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
binder_nfc_plugin_adapter_death_proc(
    NfcAdapter* adapter,
    void* plugin)
{
    BinderNfcPlugin* self = THIS(plugin);
    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        BinderNfcPluginEntry* entry = value;

        if (entry->adapter == adapter) {
            GWARN("NFC adapter \"%s\" has disappeared", adapter->name);
            nfc_manager_remove_adapter(self->manager, adapter->name);
            g_hash_table_iter_remove(&it);
            break;
        }
    }
}

static
void
binder_nfc_plugin_adapter_entry_destroy(
    gpointer data)
{
    BinderNfcPluginEntry* entry = data;

    nfc_adapter_remove_handler(entry->adapter, entry->death_id);
    nfc_adapter_unref(entry->adapter);
    g_free(entry);
}

static
void
binder_nfc_plugin_add_adapter(
    BinderNfcPlugin* self,
    GBinderRemoteObject* remote,
    const BinderNfcBackend* backend,
    const char* fqname)
{
    BinderNfcApi* api = backend->api(remote);
    BinderNfcPluginEntry* entry = g_new0(BinderNfcPluginEntry, 1);

    entry->adapter = binder_nfc_adapter_new(api);
    entry->death_id = binder_nfc_adapter_add_death_handler(entry->adapter,
        binder_nfc_plugin_adapter_death_proc, self);
    g_hash_table_insert(self->adapters, g_strdup(fqname), entry);
    nfc_manager_add_adapter(self->manager, entry->adapter);
    GINFO("NFC adapter %s (%s) => \"%s\"", fqname, backend->name,
        entry->adapter->name);
    g_object_unref(api);
}

static
void
binder_nfc_plugin_start_watch_entry_destroy(
    gpointer data)
{
    BinderNfcStartWatchEntry* entry = data;

    g_signal_handler_disconnect(entry->watcher, entry->watch_id);
    g_object_unref(entry->watcher);
    g_free(entry);
}

static
void
binder_nfc_plugin_watch_proc(
    BinderNfcWatcher* watcher,
    GBinderRemoteObject* remote,
    const char* fqname,
    gpointer plugin)
{
    binder_nfc_plugin_add_adapter(THIS(plugin), remote, watcher->backend,
        fqname);
}

static
void
binder_nfc_plugin_start_watch_proc(
    BinderNfcWatcher* watcher,
    GBinderRemoteObject* remote,
    const char* fqname,
    gpointer plugin)
{
    BinderNfcPlugin* self = THIS(plugin);

    /* From this point on we keep using the watcher which found the first
     * NFC service. */
    GDEBUG("Using %s backend", watcher->backend->name);
    GASSERT(!self->watcher);
    g_object_ref(self->watcher = watcher);
    self->watch_id = binder_nfc_watcher_add_handler(watcher,
        binder_nfc_plugin_watch_proc, self);

    /* Drop the initial watchers */
    g_slist_free_full(self->start_watches,
        binder_nfc_plugin_start_watch_entry_destroy);
    self->start_watches = NULL;

    /* Add this adapter */
    binder_nfc_plugin_add_adapter(self, remote, watcher->backend, fqname);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gboolean
binder_nfc_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    guint i;
    BinderNfcPlugin* self = THIS(plugin);

    GASSERT(!self->start_watches);
    GASSERT(!self->watcher);
    for (i = 0; i < N_BACKENDS; i++) {
        BinderNfcStartWatchEntry* entry = g_new0(BinderNfcStartWatchEntry, 1);

        entry->watcher = binder_nfc_watcher_new(binder_nfc_backends + i);
        entry->watch_id = binder_nfc_watcher_add_handler(entry->watcher,
            binder_nfc_plugin_start_watch_proc, self);
        self->start_watches = g_slist_append(self->start_watches, entry);
    }
    self->manager = nfc_manager_ref(manager);
    return TRUE;
}

static
void
binder_nfc_plugin_stop(
    NfcPlugin* plugin)
{
    BinderNfcPlugin* self = THIS(plugin);

    GVERBOSE("Stopping");
    if (self->start_watches) {
        g_slist_free_full(self->start_watches,
            binder_nfc_plugin_start_watch_entry_destroy);
        self->start_watches = NULL;
    }
    if (self->watcher) {
        g_signal_handler_disconnect(self->watcher, self->watch_id);
        g_object_unref(self->watcher);
        self->watch_id = 0;
        self->watcher = NULL;
    }
    if (self->manager) {
        GHashTableIter it;
        gpointer value;

        g_hash_table_iter_init(&it, self->adapters);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            BinderNfcPluginEntry* entry = value;

            nfc_manager_remove_adapter(self->manager, entry->adapter->name);
            g_hash_table_iter_remove(&it);
        }
        nfc_manager_unref(self->manager);
        self->manager = NULL;
    }
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_plugin_init(
    BinderNfcPlugin* self)
{
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, binder_nfc_plugin_adapter_entry_destroy);
}

static
void
binder_nfc_plugin_finalize(
    GObject* object)
{
    BinderNfcPlugin* self = THIS(object);

    g_hash_table_destroy(self->adapters);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
binder_nfc_plugin_class_init(
    NfcPluginClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = binder_nfc_plugin_finalize;
    klass->start = binder_nfc_plugin_start;
    klass->stop = binder_nfc_plugin_stop;
}

/*==========================================================================*
 * API
 *==========================================================================*/

static
NfcPlugin*
binder_nfc_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(THIS_TYPE, NULL);
}

static GLogModule* const binder_nfc_plugin_logs[] = {
    &GLOG_MODULE_NAME,
    &binder_hexdump_log,
    &GBINDER_LOG_MODULE,
    &NCI_LOG_MODULE,
    NULL
};

NFC_PLUGIN_DEFINE2(binder, "binder integration", binder_nfc_plugin_create,
    binder_nfc_plugin_logs, 0)

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
