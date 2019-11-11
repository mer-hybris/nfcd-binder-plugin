/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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

#include "binder_nfc_plugin.h"
#include "binder_nfc.h"
#include "plugin.h"

#include <nfc_adapter.h>
#include <nfc_manager.h>
#include <nfc_plugin_impl.h>

#include <nci_types.h>

#include <gbinder.h>

#include <gutil_misc.h>

GLOG_MODULE_DEFINE("binder");

#define BINDER_NFC_EXPORT __attribute__ ((visibility("default")))

typedef struct binder_nfc_plugin_adapter_entry {
    char* instance;
    gulong death_id;
    gulong powering_off;
    NfcAdapter* adapter;
} BinderNfcPluginEntry;

typedef NfcPluginClass BinderNfcPluginClass;
struct binder_nfc_plugin {
    NfcPlugin parent;
    GBinderServiceManager* sm;
    NfcManager* manager;
    GHashTable* adapters;
    gulong name_watch_id;
    gulong list_call_id;
    gint blocked;
    gboolean actually_blocked;
};

G_DEFINE_TYPE(BinderNfcPlugin, binder_nfc_plugin, NFC_TYPE_PLUGIN)
#define BINDER_TYPE_PLUGIN (binder_nfc_plugin_get_type())
#define BINDER_NFC_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        BINDER_TYPE_PLUGIN, BinderNfcPlugin))
#define BINDER_IS_NFC_PLUGIN(obj) G_TYPE_CHECK_INSTANCE_TYPE(obj, \
        BINDER_TYPE_PLUGIN)

enum nfc_manager_signal {
    SIGNAL_BLOCKED_CHANGED,
    SIGNAL_COUNT
};

#define SIGNAL_BLOCKED_CHANGED_NAME "binder-nfc-plugin-blocked-changed"

static guint binder_nfc_plugin_signals[SIGNAL_COUNT] = { 0 };

struct binder_nfc_plugin_block {
    BinderNfcPlugin* binder;
};

static
void
binder_nfc_plugin_update_blocked(
    BinderNfcPlugin* self);

static
void
binder_nfc_plugin_update_entry(
    BinderNfcPlugin* self,
    BinderNfcPluginEntry* entry);

BINDER_NFC_EXPORT
BinderNfcPlugin*
binder_nfc_plugin_cast(
    NfcPlugin* plugin)
{
    return (G_LIKELY(plugin) && BINDER_IS_NFC_PLUGIN(plugin)) ?
        BINDER_NFC_PLUGIN(plugin) : NULL;
}

BINDER_NFC_EXPORT
BinderNfcPlugin*
binder_nfc_plugin_ref(
    BinderNfcPlugin* self)
{
    if (G_LIKELY(self)) {
        nfc_plugin_ref(NFC_PLUGIN(self));
    }
    return self;
}

BINDER_NFC_EXPORT
void
binder_nfc_plugin_unref(
    BinderNfcPlugin* self)
{
    if (G_LIKELY(self)) {
        nfc_plugin_unref(NFC_PLUGIN(self));
    }
}

BINDER_NFC_EXPORT
BinderNfcPluginBlock*
binder_nfc_plugin_block_new(
    BinderNfcPlugin* self)
{
    if (G_LIKELY(self)) {
        BinderNfcPluginBlock* block = g_slice_new0(BinderNfcPluginBlock);

        block->binder = binder_nfc_plugin_ref(self);
        self->blocked++;
        binder_nfc_plugin_update_blocked(self);
        return block;
    }
    return NULL;
}

BINDER_NFC_EXPORT
void
binder_nfc_plugin_block_free(
    BinderNfcPluginBlock* block)
{
    if (G_LIKELY(block)) {
        BinderNfcPlugin* self = block->binder;

        GASSERT(self->blocked > 0);
        self->blocked--;
        binder_nfc_plugin_update_blocked(self);
        binder_nfc_plugin_unref(self);
        g_slice_free1(sizeof(*block), block);
    }
}

BINDER_NFC_EXPORT
gboolean
binder_nfc_plugin_is_blocked(
    BinderNfcPlugin* binder)
{
    return G_LIKELY(binder) && binder->actually_blocked;
}

BINDER_NFC_EXPORT
gulong
binder_nfc_plugin_add_blocked_handler(
    BinderNfcPlugin* binder,
    BinderNfcPluginFunc callback,
    void* user_data)
{
    return (G_LIKELY(binder) && G_LIKELY(callback)) ? g_signal_connect(binder,
        SIGNAL_BLOCKED_CHANGED_NAME, G_CALLBACK(callback), user_data) : 0;
}

BINDER_NFC_EXPORT
void
binder_nfc_plugin_remove_handler(
    BinderNfcPlugin* binder,
    gulong id)
{
    if (G_LIKELY(binder) && G_LIKELY(id)) {
        g_signal_handler_disconnect(binder, id);
    }
}

static
NfcAdapter*
binder_nfc_plugin_detach_adapter(
    BinderNfcPluginEntry* entry)
{
    NfcAdapter* adapter = entry->adapter;

    nfc_adapter_remove_handler(adapter, entry->powering_off);
    nfc_adapter_remove_handler(adapter, entry->death_id);
    entry->powering_off = 0;
    entry->death_id = 0;
    entry->adapter = NULL;
    return adapter; /* Caller has to unref it */
}

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
            NfcAdapter* adapter = binder_nfc_plugin_detach_adapter(entry);

            GWARN("NFC adapter \"%s\" has disappeared", (char*)key);
            g_hash_table_iter_remove(&it);
            nfc_manager_remove_adapter(self->manager, adapter->name);
            nfc_adapter_unref(adapter);
            break;
        }
    }
}

static
void
binder_nfc_plugin_update_blocked(
    BinderNfcPlugin* self)
{
    guint adapters = 0, powering_off = 0;
    gboolean actually_blocked;
    gpointer value;
    GHashTableIter it;

    g_hash_table_iter_init(&it, self->adapters);
    while (g_hash_table_iter_next(&it, NULL, &value)) {
        BinderNfcPluginEntry* entry = value;

        binder_nfc_plugin_update_entry(self, entry);
        if (entry->adapter) {
            adapters++;
            if (entry->powering_off) {
                powering_off++;
            }
        }
    }

    actually_blocked = (self->blocked && !powering_off);
    if (self->actually_blocked != actually_blocked) {
        self->actually_blocked = actually_blocked;
        g_signal_emit(self, binder_nfc_plugin_signals
            [SIGNAL_BLOCKED_CHANGED], 0);
    }
}

static
void
binder_nfc_plugin_powered_off(
    NfcAdapter* adapter,
    void* plugin)
{
    binder_nfc_plugin_update_blocked(BINDER_NFC_PLUGIN(plugin));
}

static
void
binder_nfc_plugin_update_entry(
    BinderNfcPlugin* self,
    BinderNfcPluginEntry* entry)
{
    if (entry->adapter && !entry->adapter->powered && entry->powering_off) {
        /* Done with powering off */
        nfc_adapter_unref(binder_nfc_plugin_detach_adapter(entry));
    }

    if (self->blocked) {
        if (entry->adapter) {
            NfcAdapter* adapter = nfc_adapter_ref(entry->adapter);

            nfc_manager_remove_adapter(self->manager, adapter->name);
            if (adapter->powered) {
                /* Close it (power off) before actually freeing it */
                if (!entry->powering_off) {
                    entry->powering_off =
                        nfc_adapter_add_powered_changed_handler(adapter,
                            binder_nfc_plugin_powered_off, self);
                    nfc_adapter_request_power(adapter, FALSE);
                }
            } else {
                /* Can drop it */
                nfc_adapter_unref(binder_nfc_plugin_detach_adapter(entry));
            }
            nfc_adapter_unref(adapter);
        }
    } else if (!entry->adapter) {
        entry->adapter = binder_nfc_adapter_new(self->sm, entry->instance);

        if (entry->adapter) {
            GINFO("NFC adapter \"%s\"", entry->instance);
            entry->death_id = binder_nfc_adapter_add_death_handler
                (entry->adapter, binder_nfc_plugin_adapter_death_proc, self);
            nfc_manager_add_adapter(self->manager, entry->adapter);
        }
    }
}

static
void
binder_nfc_plugin_adapter_entry_free(
    gpointer data)
{
    BinderNfcPluginEntry* entry = data;

    nfc_adapter_unref(binder_nfc_plugin_detach_adapter(entry));
    g_free(entry->instance);
    g_slice_free1(sizeof(*entry), entry);
}

static
void
binder_nfc_plugin_add_adapter(
    BinderNfcPlugin* self,
    const char* instance)
{
    BinderNfcPluginEntry* entry = g_hash_table_lookup(self->adapters, instance);

    if (!entry) {
        GINFO("NFC adapter \"%s\"", instance);
        entry = g_slice_new0(BinderNfcPluginEntry);
        entry->instance = g_strdup(instance);
        g_hash_table_insert(self->adapters, entry->instance, entry);
    }
    binder_nfc_plugin_update_entry(self, entry);
}

static
gboolean
binder_nfc_plugin_service_list_proc(
    GBinderServiceManager* sm,
    char** services,
    void* plugin)
{
    BinderNfcPlugin* self = BINDER_NFC_PLUGIN(plugin);
    char** ptr;

    self->list_call_id = 0;
    for (ptr = services; *ptr; ptr++) {
        if (g_str_has_prefix(*ptr, BINDER_NFC)) {
            const char* sep = strchr(*ptr, '/');

            if (sep && sep[1]) {
                binder_nfc_plugin_add_adapter(self, sep + 1);
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

    self->sm = gbinder_hwservicemanager_new(NULL);
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
        GPtrArray* adapters = NULL;

        g_hash_table_iter_init(&it, self->adapters);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            NfcAdapter* adapter = binder_nfc_plugin_detach_adapter(value);

            if (adapter) {
                if (!adapters) {
                    adapters = g_ptr_array_new_with_free_func((GDestroyNotify)
                        nfc_adapter_unref);
                }
                g_ptr_array_add(adapters, adapter);
            }
        }
        g_hash_table_remove_all(self->adapters);
        if (adapters) {
            guint i;

            for (i = 0; i < adapters->len; i++) {
                NfcAdapter* adapter = adapters->pdata[i];

                nfc_manager_remove_adapter(self->manager, adapter->name);
            }
            g_ptr_array_free(adapters, TRUE);
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
    self->adapters = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
        binder_nfc_plugin_adapter_entry_free);
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
    binder_nfc_plugin_signals[SIGNAL_BLOCKED_CHANGED] =
        g_signal_new(SIGNAL_BLOCKED_CHANGED_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
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
    &NCI_LOG_MODULE,
    NULL
};

const NfcPluginDesc NFC_PLUGIN_DESC(binder) NFC_PLUGIN_DESC_ATTR = {
    BINDER_NFC_PLUGIN_NAME, "binder integration", NFC_CORE_VERSION,
    binder_nfc_plugin_create, binder_nfc_plugin_logs, 0
};

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
