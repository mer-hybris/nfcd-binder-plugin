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

#include "binder_nfc_api_hidl.h"
#include "binder_nfc_api_impl.h"

#include <gbinder.h>

typedef BinderNfcApiClass BinderNfcApiHidlClass;
typedef struct binder_nfc_api_hidl {
    BinderNfcApi parent;
    GBinderLocalObject* callback;
} BinderNfcApiHidl;

#define PARENT_CLASS binder_nfc_api_hidl_parent_class
#define PARENT_TYPE BINDER_NFC_TYPE_API
#define THIS_TYPE binder_nfc_api_hidl_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj,THIS_TYPE,BinderNfcApiHidl)

GType THIS_TYPE G_GNUC_INTERNAL;
G_DEFINE_TYPE(BinderNfcApiHidl, binder_nfc_api_hidl, PARENT_TYPE)

/* android.hardware.nfc@1.0::INfc */
typedef enum binder_nfc_hidl_req {
    BINDER_NFC_HIDL_REQ_OPEN = 1,          /*  open */
    BINDER_NFC_HIDL_REQ_WRITE,             /* write */
    BINDER_NFC_HIDL_REQ_CORE_INITIALIZED,  /* coreInitialized */
    BINDER_NFC_HIDL_REQ_PREDISCOVER,       /* prediscover */
    BINDER_NFC_HIDL_REQ_CLOSE,             /* close */
    BINDER_NFC_HIDL_REQ_CONTROL_GRANTED,   /* controlGranted */
    BINDER_NFC_HIDL_REQ_POWER_CYCLE        /* powerCycle */
} BINDER_NFC_HIDL_REQ;

/* android.hardware.nfc@1.0::INfcClientCallback */
#define BINDER_NFC_HIDL_CALLBACK_IFACE \
    BINDER_NFC_HIDL_IFACE_("INfcClientCallback")
typedef enum binder_nfc_hidl_callback_req {
    BINDER_NFC_HIDL_CALLBACK_REQ_SEND_EVENT =  1, /* sendEvent */
    BINDER_NFC_HIDL_CALLBACK_REQ_SEND_DATA        /* sendData */
} BINDER_NFC_HIDL_CALLBACK_REQ;

#define BINDER_NFC_HIDL_EVENTS(e) \
    e(OPEN_CPLT) \
    e(CLOSE_CPLT) \
    e(POST_INIT_CPLT) \
    e(PRE_DISCOVER_CPLT) \
    e(REQUEST_CONTROL) \
    e(RELEASE_CONTROL) \
    e(ERROR)

enum binder_nfc_api_hidl_event {
    #define NFC_HIDL_EVT(x) NFC_HIDL_EVT_##x,
    BINDER_NFC_HIDL_EVENTS(NFC_HIDL_EVT)
    #undef NFC_HIDL_EVT
};

/*==========================================================================*
 * API for BinderNfcPlugin
 *==========================================================================*/

BinderNfcApi*
binder_nfc_api_hidl_new(
    GBinderRemoteObject* remote)
{
    BinderNfcApiHidl* self = g_object_new(THIS_TYPE, NULL);
    BinderNfcApi* api = &self->parent;
    GBinderClient* client = gbinder_client_new(remote, BINDER_NFC_HIDL_IFACE);

    binder_nfc_api_init_base(api, client, remote);
    gbinder_client_unref(client);
    return api;
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
int
binder_nfc_api_hidl_handle_event(
    BinderNfcApiHidl* self,
    GBinderReader* reader)
{
    guint32 event, status;

    if (gbinder_reader_read_uint32(reader, &event) &&
        gbinder_reader_read_uint32(reader, &status) &&
        gbinder_reader_at_end(reader)) {
        BinderNfcApi* api = &self->parent;

#if GUTIL_LOG_DEBUG
        if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
            switch (event) {
            #define DUMP_HIDL_EVT(x) \
            case  NFC_HIDL_EVT_##x: GDEBUG("> " #x); break;
            BINDER_NFC_HIDL_EVENTS(DUMP_HIDL_EVT)
            #undef DUMP_HIDL_EVT
            default:
                GDEBUG("> event %u", event);
                break;
            }
        }
#endif /* GUTIL_LOG_DEBUG */

        switch (event) {
        case NFC_HIDL_EVT_OPEN_CPLT:
            binder_nfc_api_emit_event(api, BINDER_NFC_EVENT_OPEN_CPLT);
            break;
        case NFC_HIDL_EVT_CLOSE_CPLT:
            binder_nfc_api_emit_event(api, BINDER_NFC_EVENT_CLOSE_CPLT);
            break;
        default:
            break;
        }
        return GBINDER_STATUS_OK;
    } else {
        GWARN("Failed to parse INfcClientCallback::sendEvent payload");
        return GBINDER_STATUS_FAILED;
    }
}

static
int
binder_nfc_api_hidl_handle_data(
    BinderNfcApiHidl* self,
    GBinderReader* reader)
{
    gsize len;
    const guint8* data = gbinder_reader_read_hidl_byte_vec(reader, &len);

    if (data && gbinder_reader_at_end(reader)) {
        binder_nfc_api_emit_data(&self->parent, data, len);
        return GBINDER_STATUS_OK;
    } else {
        GWARN("Failed to parse INfcClientCallback::sendData payload");
        return GBINDER_STATUS_FAILED;
    }
}

static
GBinderLocalReply*
binder_nfc_api_hidl_callback_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    BinderNfcApiHidl* self = THIS(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, BINDER_NFC_HIDL_CALLBACK_IFACE)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader(req, &reader);
        switch (code) {
        case BINDER_NFC_HIDL_CALLBACK_REQ_SEND_EVENT:
            GDEBUG("%s %u sendEvent", iface, code);
            *status = binder_nfc_api_hidl_handle_event(self, &reader);
            break;
        case BINDER_NFC_HIDL_CALLBACK_REQ_SEND_DATA:
            GDEBUG("%s %u sendData", iface, code);
            *status = binder_nfc_api_hidl_handle_data(self, &reader);
            break;
        default:
            GDEBUG("%s %u", iface, code);
            *status = GBINDER_STATUS_FAILED;
            break;
        }
    } else {
        GDEBUG("%s %u", iface, code);
        *status = GBINDER_STATUS_FAILED;
    }
    return (*status == GBINDER_STATUS_OK) ? gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(obj), 0) : NULL;
}

static
void
binder_nfc_api_hidl_complete(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* call)
{
    int result = -1;

    /* Generic completion */
    binder_nfc_api_call_complete(call,
        status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result) &&
        result == 0);
}

static
gulong
binder_nfc_api_hidl_call(
    BinderNfcApi* api,
    BINDER_NFC_HIDL_REQ code,
    GBinderLocalRequest* req,  /* gets unref'd */
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    gulong id = gbinder_client_transact(api->client, code, 0, req,
        binder_nfc_api_hidl_complete, binder_nfc_api_call_destroy,
        binder_nfc_api_call_new(api, complete, destroy, user_data));

    gbinder_local_request_unref(req);
    return id;
}

static
void
binder_nfc_api_hidl_close_complete(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    BinderNfcApiCall* call = user_data;
    BinderNfcApiHidl* self = THIS(call->api);

    /* We can release our local object now */
    gbinder_local_object_drop(self->callback);
    self->callback = NULL;
    binder_nfc_api_hidl_complete(client, reply, status, call);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gulong
binder_nfc_api_hidl_open(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    BinderNfcApiHidl* self = THIS(api);
    GBinderLocalRequest* req = gbinder_client_new_request(api->client);

    if (!self->callback) {
        GBinderIpc* ipc = gbinder_remote_object_ipc(api->remote);
        static const char* ifaces[] = { BINDER_NFC_HIDL_CALLBACK_IFACE, NULL };

        self->callback = gbinder_local_object_new(ipc, ifaces,
            binder_nfc_api_hidl_callback_handler, self);
    }

    gbinder_local_request_append_local_object(req, self->callback);

    /* binder_nfc_api_hidl_call unrefs the request */
    return binder_nfc_api_hidl_call(api,
        BINDER_NFC_HIDL_REQ_OPEN, req,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_hidl_close(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return gbinder_client_transact(api->client,
        BINDER_NFC_HIDL_REQ_CLOSE, 0, NULL,
        binder_nfc_api_hidl_close_complete,
        binder_nfc_api_call_destroy,
        binder_nfc_api_call_new(api, complete, destroy, user_data));
}

static
gulong
binder_nfc_api_hidl_core_initialized(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return binder_nfc_api_hidl_call(api,
        BINDER_NFC_HIDL_REQ_CORE_INITIALIZED, NULL,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_hidl_prediscover(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return binder_nfc_api_hidl_call(api,
        BINDER_NFC_HIDL_REQ_PREDISCOVER, NULL,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_hidl_write(
    BinderNfcApi* api,
    const void* data,
    gsize len,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    GBinderLocalRequest* req = gbinder_client_new_request(api->client);
    GBinderWriter writer;

    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_vec(&writer, data, len, 1);

    /* binder_nfc_api_hidl_call unrefs the request */
    return binder_nfc_api_hidl_call(api,
        BINDER_NFC_HIDL_REQ_WRITE, req,
        complete, destroy, user_data);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_api_hidl_init(
    BinderNfcApiHidl* self)
{
}

static
void
binder_nfc_api_hidl_finalize(
    GObject* object)
{
    BinderNfcApiHidl* self = THIS(object);

    gbinder_local_object_drop(self->callback);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
binder_nfc_api_hidl_class_init(
    BinderNfcApiHidlClass* klass)
{
    klass->open = binder_nfc_api_hidl_open;
    klass->close = binder_nfc_api_hidl_close;
    klass->core_initialized = binder_nfc_api_hidl_core_initialized;
    klass->prediscover = binder_nfc_api_hidl_prediscover;
    klass->write = binder_nfc_api_hidl_write;
    G_OBJECT_CLASS(klass)->finalize = binder_nfc_api_hidl_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
