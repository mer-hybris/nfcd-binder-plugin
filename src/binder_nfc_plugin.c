/*
 * Copyright (C) 2018-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2020 Jolla Ltd.
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "binder_nfc.h"
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

typedef NfcPluginClass BinderNfcPluginClass;
typedef struct binder_nfc_plugin {
    NfcPlugin parent;
    GBinderServiceManager* sm;
    NfcManager* manager;
    GHashTable* adapters;
    gulong name_watch_id;
    gulong list_call_id;
} BinderNfcPlugin;

G_DEFINE_TYPE(BinderNfcPlugin, binder_nfc_plugin, NFC_TYPE_PLUGIN)
#define BINDER_TYPE_PLUGIN (binder_nfc_plugin_get_type())
#define BINDER_NFC_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        BINDER_TYPE_PLUGIN, BinderNfcPlugin))

static
void
binder_nfc_plugin_adapter_death_proc(
    NfcAdapter* adapter,
    void* plugin)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);
    GHashTableIter it;
    gpointer key, value;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, &key, &value)) {
        BinderNfcPluginEntry* entry = value;

        if (entry->adapter == value) {
            GWARN("NFC adapter \"%s\" has disappeared", (char*)key);
            nfc_manager_remove_adapter(self->manager, adapter->name);
            g_hash_table_iter_remove(&it);
            break;
        }
    }
}

static
void
binder_nfc_plugin_adapter_entry_free(
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
    const char* instance)
{
    if (instance[0] && !g_hash_table_contains(self->adapters, instance)) {
        NfcAdapter* adapter = binder_nfc_adapter_new(self->sm, instance);

        if (adapter) {
            BinderNfcPluginEntry* entry = g_new0(BinderNfcPluginEntry, 1);

            GINFO("NFC adapter \"%s\"", instance);
            entry->adapter = adapter;
            entry->death_id = binder_nfc_adapter_add_death_handler(adapter,
                binder_nfc_plugin_adapter_death_proc, self);
            g_hash_table_insert(self->adapters, g_strdup(instance), entry);
            nfc_manager_add_adapter(self->manager, adapter);
        }
    }
}

static
gboolean
binder_nfc_plugin_service_list_proc(
    GBinderServiceManager* sm,
    char** services,
    void* plugin)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);

    self->list_call_id = 0;
    if (services) {
        char** ptr;

        for (ptr = services; *ptr; ptr++) {
            if (g_str_has_prefix(*ptr, BINDER_NFC)) {
                const char* sep = strchr(*ptr, '/');

                if (sep) {
                    binder_nfc_plugin_add_adapter(self, sep + 1);
                }
            }
        }
    }
    return FALSE;
}

static
void
binder_nfc_plugin_service_registration_proc(
    GBinderServiceManager* sm,
    const char* name,
    void* plugin)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);

    if (!self->list_call_id) {
        self->list_call_id = gbinder_servicemanager_list(self->sm,
            binder_nfc_plugin_service_list_proc, self);
    }
}

static
gboolean
binder_nfc_plugin_start(
    NfcPlugin* plugin,
    NfcManager* manager)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);
    GASSERT(!self->sm);

    self->sm = gbinder_servicemanager_new(GBINDER_DEFAULT_HWBINDER);
    if (self->sm) {
        GVERBOSE("Starting");
        self->manager = nfc_manager_ref(manager);
        self->name_watch_id =
            gbinder_servicemanager_add_registration_handler(self->sm,
                BINDER_NFC, binder_nfc_plugin_service_registration_proc, self);
        self->list_call_id =
            gbinder_servicemanager_list(self->sm,
                binder_nfc_plugin_service_list_proc, self);
        return TRUE;
    } else {
        GERR("Failed to connect to hwservicemanager");
        return FALSE;
    }
}

static
void
binder_nfc_plugin_stop(
    NfcPlugin* plugin)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);

    GVERBOSE("Stopping");
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
        if (self->list_call_id) {
            gbinder_servicemanager_cancel(self->sm, self->list_call_id);
            self->list_call_id = 0;
        }
        if (self->name_watch_id) {
            gbinder_servicemanager_remove_handler(self->sm,
                self->name_watch_id);
            self->name_watch_id = 0;
        }
    }
}

static
void
binder_nfc_plugin_init(
    BinderNfcPlugin* self)
{
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, binder_nfc_plugin_adapter_entry_free);
}

static
void
binder_nfc_plugin_finalize(
    GObject* object)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(object);

    g_hash_table_destroy(self->adapters);
    gbinder_servicemanager_remove_handler(self->sm, self->name_watch_id);
    gbinder_servicemanager_cancel(self->sm, self->list_call_id);
    gbinder_servicemanager_unref(self->sm);
    G_OBJECT_CLASS(binder_nfc_plugin_parent_class)->finalize(object);
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

static
NfcPlugin*
binder_nfc_plugin_create(
    void)
{
    GDEBUG("Plugin loaded");
    return g_object_new(BINDER_TYPE_PLUGIN, NULL);
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
