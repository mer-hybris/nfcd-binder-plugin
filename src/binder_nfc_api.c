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

#include "binder_nfc_api_impl.h"

#include <gbinder.h>
#include <gutil_macros.h>
#include <gutil_misc.h>

#define PARENT_CLASS binder_nfc_api_parent_class
#define PARENT_TYPE G_TYPE_OBJECT
#define THIS(obj) BINDER_NFC_API(obj)
#define THIS_TYPE BINDER_NFC_TYPE_API
#define GET_THIS_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, THIS_TYPE, \
        BinderNfcApiClass)

G_DEFINE_TYPE(BinderNfcApi, binder_nfc_api, PARENT_TYPE)

enum binder_nfc_api_signal {
    SIGNAL_EVENT,
    SIGNAL_DATA,
    SIGNAL_COUNT
};

#define SIGNAL_EVENT_NAME   "binder-nfc-api-event"
#define SIGNAL_DATA_NAME    "binder-nfc-api-data"

static guint binder_nfc_api_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * BinderNfcApiCall
 *==========================================================================*/

typedef struct binder_nfc_api_call_impl {
    BinderNfcApiCall call;
    BinderNfcApiCompleteFunc complete;
    GDestroyNotify destroy;
    gpointer user_data;
} BinderNfcApiCallImpl;

BinderNfcApiCall*
binder_nfc_api_call_new(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    BinderNfcApiCallImpl* impl = g_slice_new(BinderNfcApiCallImpl);

    g_object_ref(impl->call.api = api);
    impl->complete = complete;
    impl->destroy = destroy;
    impl->user_data = user_data;
    return &impl->call;
}

void
binder_nfc_api_call_complete(
    BinderNfcApiCall* call,
    gboolean ok)
{
    BinderNfcApiCallImpl* impl = G_CAST(call,BinderNfcApiCallImpl,call);

    if (impl->complete) {
        impl->complete(call->api, ok, impl->user_data);
    }
}

void
binder_nfc_api_call_destroy(
    gpointer call)
{
    BinderNfcApiCallImpl* impl = call;

    if (impl->destroy) {
        impl->destroy(impl->user_data);
    }
    g_object_unref(impl->call.api);
    gutil_slice_free(impl);
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gulong
binder_nfc_api_fail(
    BinderNfcApi* self,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (destroy) {
        destroy(user_data);
    }
    return 0;
}

/*==========================================================================*
 * API for BinderNfcAdapter
 *==========================================================================*/

gulong
binder_nfc_api_open(
    BinderNfcApi* self,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        gulong id = GET_THIS_CLASS(self)->open(self, complete, destroy,
            user_data);

        if (id) {
            return id;
        }
    }
    return binder_nfc_api_fail(self, destroy, user_data);
}

gulong
binder_nfc_api_close(
    BinderNfcApi* self,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        gulong id = GET_THIS_CLASS(self)->close(self, complete, destroy,
            user_data);

        if (id) {
            return id;
        }
    }
    return binder_nfc_api_fail(self, destroy, user_data);
}

gulong
binder_nfc_api_core_initialized(
    BinderNfcApi* self,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        gulong id = GET_THIS_CLASS(self)->core_initialized(self, complete,
            destroy, user_data);

        if (id) {
            return id;
        }
    }
    return binder_nfc_api_fail(self, destroy, user_data);
}

gulong
binder_nfc_api_prediscover(
    BinderNfcApi* self,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        gulong id = GET_THIS_CLASS(self)->prediscover(self, complete,
            destroy, user_data);

        if (id) {
            return id;
        }
    }
    return binder_nfc_api_fail(self, destroy, user_data);
}

gulong
binder_nfc_api_write(
    BinderNfcApi* self,
    const void* data,
    gsize len,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    if (G_LIKELY(self)) {
        gulong id = GET_THIS_CLASS(self)->write(self, data, len, complete,
            destroy, user_data);

        if (id) {
            return id;
        }
    }
    return binder_nfc_api_fail(self, destroy, user_data);
}

gulong
binder_nfc_api_add_event_handler(
    BinderNfcApi* self,
    BINDER_NFC_EVENT event,
    BinderNfcApiEventFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ?
        g_signal_connect_closure_by_id(self,
            binder_nfc_api_signals[SIGNAL_EVENT], event,
            g_cclosure_new(G_CALLBACK(func), user_data, NULL), FALSE) : 0;
}

gulong
binder_nfc_api_add_data_handler(
    BinderNfcApi* self,
    BinderNfcApiDataFunc func,
    void* user_data)
{
    return (G_LIKELY(self) && G_LIKELY(func)) ? g_signal_connect(self,
        SIGNAL_DATA_NAME, G_CALLBACK(func), user_data) : 0;
}

/*==========================================================================*
 * Internal API for derived classes
 *==========================================================================*/

void
binder_nfc_api_init_base(
    BinderNfcApi* self,
    GBinderClient* client,
    GBinderRemoteObject* remote)
{
    self->client = gbinder_client_ref(client);
    self->remote = gbinder_remote_object_ref(remote);
}

void
binder_nfc_api_emit_event(
    BinderNfcApi* self,
    BINDER_NFC_EVENT event)
{
    g_signal_emit(self, binder_nfc_api_signals[SIGNAL_EVENT], event, event);
}

void
binder_nfc_api_emit_data(
    BinderNfcApi* self,
    const void* data,
    gsize size)
{
    g_signal_emit(self, binder_nfc_api_signals[SIGNAL_DATA], 0, data, size);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gulong
binder_nfc_api_not_implemented(
    BinderNfcApi* self,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return 0;
}

static
gulong
binder_nfc_api_write_not_implemented(
    BinderNfcApi* self,
    const void* data,
    gsize len,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return 0;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_api_init(
    BinderNfcApi* self)
{
}

static
void
binder_nfc_api_finalize(
    GObject* object)
{
    BinderNfcApi* self = THIS(object);

    gbinder_client_unref(self->client);
    gbinder_remote_object_unref(self->remote);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
binder_nfc_api_class_init(
    BinderNfcApiClass* klass)
{
    GType type = G_OBJECT_CLASS_TYPE(klass);

    klass->open = binder_nfc_api_not_implemented;
    klass->close = binder_nfc_api_not_implemented;
    klass->core_initialized = binder_nfc_api_not_implemented;
    klass->prediscover = binder_nfc_api_not_implemented;
    klass->write = binder_nfc_api_write_not_implemented;
    G_OBJECT_CLASS(klass)->finalize = binder_nfc_api_finalize;

    binder_nfc_api_signals[SIGNAL_EVENT] =
        g_signal_new(SIGNAL_EVENT_NAME, type,
            G_SIGNAL_RUN_FIRST | G_SIGNAL_DETAILED, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_INT);
    binder_nfc_api_signals[SIGNAL_DATA] =
        g_signal_new(SIGNAL_DATA_NAME, type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_ULONG);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
