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

#include "binder_nfc_api_aidl.h"
#include "binder_nfc_api_impl.h"

#include <gbinder.h>

typedef struct binder_nfc_api_aidl {
    BinderNfcApi parent;
    GBinderLocalObject* callback;
} BinderNfcApiAidl;

typedef BinderNfcApiClass BinderNfcApiAidlClass;

#define PARENT_CLASS binder_nfc_api_aidl_parent_class
#define PARENT_TYPE BINDER_NFC_TYPE_API
#define THIS_TYPE binder_nfc_api_aidl_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj,THIS_TYPE,BinderNfcApiAidl)

GType THIS_TYPE G_GNUC_INTERNAL;
G_DEFINE_TYPE(BinderNfcApiAidl, binder_nfc_api_aidl, PARENT_TYPE)

/* android.hardware.nfc.INfc */
typedef enum binder_nfc_aidl_req {
    BINDER_NFC_AIDL_REQ_OPEN = 1,         /* open */
    BINDER_NFC_AIDL_REQ_CLOSE,            /* close */
    BINDER_NFC_AIDL_REQ_CORE_INITIALIZED, /* coreInitialized */
    BINDER_NFC_AIDL_REQ_FACTORY_RESET,    /* factoryReset */
    BINDER_NFC_AIDL_REQ_GET_CONFIG,       /* getConfig */
    BINDER_NFC_AIDL_REQ_POWER_CYCLE,      /* powerCycle */
    BINDER_NFC_AIDL_REQ_PREDISCOVER,      /* prediscover */
    BINDER_NFC_AIDL_REQ_WRITE,            /* write */
    BINDER_NFC_AIDL_REQ_SET_ENABLE_VERBOSE_LOGGING,/* setEnableVerboseLogging */
    BINDER_NFC_AIDL_REQ_IS_VERBOSE_LOGGING_ENABLED,/* isVerboseLoggingEnabled */
    BINDER_NFC_AIDL_REQ_CONTROL_GRANTED   /* controlGranted */
} BINDER_NFC_AIDL_REQ;

/* android.hardware.nfc.INfcClientCallback */
#define BINDER_NFC_AIDL_CALLBACK_IFACE \
    BINDER_NFC_AIDL_IFACE_("INfcClientCallback")
typedef enum binder_nfc_aidl_callback_req {
    BINDER_NFC_AIDL_CALLBACK_REQ_SEND_DATA = 1, /* sendData */
    BINDER_NFC_AIDL_CALLBACK_REQ_SEND_EVENT     /* sendEvent */
} BINDER_NFC_AIDL_CALLBACK_REQ;

enum binder_nfc_aidl_close_type {
    BINDER_NFC_AIDL_CLOSE_DISABLE,
    BINDER_NFC_AIDL_CLOSE_HOST_SWITCHED_OFF
};

#define BINDER_NFC_AIDL_EVENTS(e) \
    e(OPEN_CPLT) \
    e(CLOSE_CPLT) \
    e(POST_INIT_CPLT) \
    e(PRE_DISCOVER_CPLT) \
    e(HCI_NETWORK_RESET) \
    e(ERROR) \
    e(REQUEST_CONTROL) \
    e(RELEASE_CONTROL)

enum binder_nfc_api_aidl_event {
    #define NFC_AIDL_EVT(x) NFC_AIDL_EVT_##x,
    BINDER_NFC_AIDL_EVENTS(NFC_AIDL_EVT)
    #undef NFC_AIDL_EVT
};

/*==========================================================================*
 * API for BinderNfcPlugin
 *==========================================================================*/

BinderNfcApi*
binder_nfc_api_aidl_new(
    GBinderRemoteObject* remote)
{
    BinderNfcApiAidl* self = g_object_new(THIS_TYPE, NULL);
    BinderNfcApi* api = &self->parent;
    GBinderClient* client = gbinder_client_new(remote, BINDER_NFC_AIDL_IFACE);

    binder_nfc_api_init_base(api, client, remote);
    gbinder_client_unref(client);
    return api;
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
int
binder_nfc_api_aidl_handle_event(
    BinderNfcApiAidl* self,
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
            #define HAL_NFC_DUMP_EVT(x) \
            case NFC_AIDL_EVT_##x: GDEBUG("> " #x); break;
            BINDER_NFC_AIDL_EVENTS(HAL_NFC_DUMP_EVT)
            #undef HAL_NFC_DUMP_EVT
            default:
                GDEBUG("> event %u", event);
                break;
            }
        }
#endif /* GUTIL_LOG_DEBUG */

        switch (event) {
        case NFC_AIDL_EVT_OPEN_CPLT:
            binder_nfc_api_emit_event(api, BINDER_NFC_EVENT_OPEN_CPLT);
            break;
        case NFC_AIDL_EVT_CLOSE_CPLT:
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
binder_nfc_api_aidl_handle_data(
    BinderNfcApiAidl* self,
    GBinderReader* reader)
{
    gsize len;
    const guint8* data = gbinder_reader_read_byte_array(reader, &len);

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
binder_nfc_api_aidl_callback_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    BinderNfcApiAidl* self = THIS(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (!g_strcmp0(iface, BINDER_NFC_AIDL_CALLBACK_IFACE)) {
        GBinderReader reader;

        gbinder_remote_request_init_reader(req, &reader);
        switch (code) {
        case BINDER_NFC_AIDL_CALLBACK_REQ_SEND_EVENT:
            GDEBUG("%s %u sendEvent", iface, code);
            *status = binder_nfc_api_aidl_handle_event(self, &reader);
            break;
        case BINDER_NFC_AIDL_CALLBACK_REQ_SEND_DATA:
            GDEBUG("%s %u sendData", iface, code);
            *status = binder_nfc_api_aidl_handle_data(self, &reader);
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
binder_nfc_api_aidl_complete(
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
binder_nfc_api_aidl_call(
    BinderNfcApi* api,
    BINDER_NFC_AIDL_REQ code,
    GBinderLocalRequest* req,  /* gets unref'd */
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    gulong id = gbinder_client_transact(api->client, code, 0, req,
        binder_nfc_api_aidl_complete, binder_nfc_api_call_destroy,
        binder_nfc_api_call_new(api, complete, destroy, user_data));

    gbinder_local_request_unref(req);
    return id;
}

static
void
binder_nfc_api_aidl_close_complete(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    BinderNfcApiCall* call = user_data;
    BinderNfcApiAidl* self = THIS(call->api);

    /* We can release our local object now */
    gbinder_local_object_drop(self->callback);
    self->callback = NULL;
    binder_nfc_api_aidl_complete(client, reply, status, call);
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
gulong
binder_nfc_api_aidl_open(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    BinderNfcApiAidl* self = THIS(api);
    GBinderLocalRequest* req = gbinder_client_new_request(api->client);

    if (!self->callback) {
        GBinderIpc* ipc = gbinder_remote_object_ipc(api->remote);
        static const char* ifaces[] = { BINDER_NFC_AIDL_CALLBACK_IFACE, NULL };

        self->callback = gbinder_local_object_new(ipc, ifaces,
            binder_nfc_api_aidl_callback_handler, self);
        gbinder_local_object_set_stability(self->callback,
            GBINDER_STABILITY_VINTF);
    }

    gbinder_local_request_append_local_object(req, self->callback);

    /* binder_nfc_api_aidl_call unrefs the request */
    return binder_nfc_api_aidl_call(api,
        BINDER_NFC_AIDL_REQ_OPEN, req,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_aidl_close(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    GBinderLocalRequest* req = gbinder_client_new_request(api->client);

    gbinder_local_request_append_int32(req, BINDER_NFC_AIDL_CLOSE_DISABLE);

    /* binder_nfc_api_aidl_call unrefs the request */
    return gbinder_client_transact(api->client,
        BINDER_NFC_AIDL_REQ_CLOSE, 0, NULL,
        binder_nfc_api_aidl_close_complete,
        binder_nfc_api_call_destroy,
        binder_nfc_api_call_new(api, complete, destroy, user_data));
}

static
gulong
binder_nfc_api_aidl_core_initialized(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return binder_nfc_api_aidl_call(api,
        BINDER_NFC_AIDL_REQ_CORE_INITIALIZED, NULL,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_aidl_prediscover(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
{
    return binder_nfc_api_aidl_call(api,
        BINDER_NFC_AIDL_REQ_PREDISCOVER, NULL,
        complete, destroy, user_data);
}

static
gulong
binder_nfc_api_aidl_write(
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
    gbinder_writer_append_byte_array(&writer, data, len);

    /* binder_nfc_api_aidl_call unrefs the request */
    return binder_nfc_api_aidl_call(api,
        BINDER_NFC_AIDL_REQ_WRITE, req,
        complete, destroy, user_data);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_api_aidl_init(
    BinderNfcApiAidl* self)
{
}

static
void
binder_nfc_api_aidl_finalize(
    GObject* object)
{
    BinderNfcApiAidl* self = THIS(object);

    gbinder_local_object_drop(self->callback);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

static
void
binder_nfc_api_aidl_class_init(
    BinderNfcApiAidlClass* klass)
{
    BinderNfcApiClass* client = BINDER_NFC_API_CLASS(klass);

    client->open = binder_nfc_api_aidl_open;
    client->close = binder_nfc_api_aidl_close;
    client->core_initialized = binder_nfc_api_aidl_core_initialized;
    client->prediscover = binder_nfc_api_aidl_prediscover;
    client->write = binder_nfc_api_aidl_write;
    G_OBJECT_CLASS(klass)->finalize = binder_nfc_api_aidl_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
