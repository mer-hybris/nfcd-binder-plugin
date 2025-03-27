/*
 * Copyright (C) 2018-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2018-2021 Jolla Ltd.
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

#include "binder_nfc.h"

#include <nci_adapter_impl.h>

#include <nci_core.h>
#include <nci_hal.h>

#include <gbinder.h>

#include <gutil_misc.h>
#include <gutil_macros.h>

/* binder_hexdump_log is a sub-module, just to turn prefix off */
GLogModule binder_hexdump_log = {
    .name = "binder-hexdump",
    .parent = &GLOG_MODULE_NAME,
    .max_level = GLOG_LEVEL_MAX,
    .level = GLOG_LEVEL_INHERIT,
    .flags = GLOG_FLAG_HIDE_NAME
};

typedef struct binder_nfc_adapter BinderNfcAdapter;
typedef NciAdapterClass BinderNfcAdapterClass;

typedef
void
(*BinderNfcAdapterFunc)(
    BinderNfcAdapter* self);

struct binder_nfc_adapter {
    NciAdapter adapter;
    GBinderRemoteObject* remote;
    GBinderClient* client;
    GBinderLocalObject* callback;
    gboolean binder_aidl;
    NciHalIo hal_io;
    NciHalClient* hal_client;
    gulong nci_write_id;
    char* fqname;
    gboolean core_initialized;
    gulong death_id;

    gboolean need_power;
    gboolean power_on;
    gboolean power_switch_pending;
    gulong pending_tx;
    BinderNfcAdapterFunc open_cplt;
    BinderNfcAdapterFunc close_cplt;
};

G_DEFINE_TYPE(BinderNfcAdapter, binder_nfc_adapter, NCI_TYPE_ADAPTER)
#define BINDER_NFC_TYPE_ADAPTER (binder_nfc_adapter_get_type())
#define BINDER_NFC_ADAPTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        BINDER_NFC_TYPE_ADAPTER, BinderNfcAdapter))
#define SUPER_CLASS binder_nfc_adapter_parent_class

enum binder_nfc_adapter_signal {
    SIGNAL_DEATH,
    SIGNAL_COUNT
};

#define SIGNAL_DEATH_NAME "death"

static guint binder_nfc_adapter_signals[SIGNAL_COUNT] = { 0 };

/* android.hardware.nfc@1.0::INfc */
#define BINDER_NFC_REQ_OPEN                 (1) /* open */
#define BINDER_NFC_REQ_WRITE                (2) /* write */
#define BINDER_NFC_REQ_CORE_INITIALIZED     (3) /* coreInitialized */
#define BINDER_NFC_REQ_PREDISCOVER          (4) /* prediscover */
#define BINDER_NFC_REQ_CLOSE                (5) /* close */
#define BINDER_NFC_REQ_CONTROL_GRANTED      (6) /* controlGranted */
#define BINDER_NFC_REQ_POWER_CYCLE          (7) /* powerCycle */

/* android.hardware.nfc@1.0::INfcClientCallback */
#define BINDER_NFC_REQ_CALLBACK_SEND_EVENT  (1) /* sendEvent */
#define BINDER_NFC_REQ_SEND_DATA            (2) /* sendData */

/* android.hardware.nfc.INfc */
#define BINDER_NFC_AIDL_REQ_OPEN                        (1) /* open */
#define BINDER_NFC_AIDL_REQ_CLOSE                       (2) /* close */
#define BINDER_NFC_AIDL_REQ_CORE_INITIALIZED            (3) /* coreInitialized */
#define BINDER_NFC_AIDL_REQ_FACTORY_RESET               (4) /* factoryReset */
#define BINDER_NFC_AIDL_REQ_GET_CONFIG                  (5) /* getConfig */
#define BINDER_NFC_AIDL_REQ_POWER_CYCLE                 (6) /* powerCycle */
#define BINDER_NFC_AIDL_REQ_PREDISCOVER                 (7) /* prediscover */
#define BINDER_NFC_AIDL_REQ_WRITE                       (8) /* write */
#define BINDER_NFC_AIDL_REQ_SET_ENABLE_VERBOSE_LOGGING  (9) /* setEnableVerboseLogging */
#define BINDER_NFC_AIDL_REQ_IS_VERBOSE_LOGGING_ENABLED  (10) /* isVerboseLoggingEnabled */
#define BINDER_NFC_AIDL_REQ_CONTROL_GRANTED             (11) /* controlGranted */

/* android.hardware.nfc.INfcClientCallback */
#define BINDER_NFC_AIDL_REQ_SEND_DATA                   (1) /* sendData */
#define BINDER_NFC_AIDL_REQ_CALLBACK_SEND_EVENT         (2) /* sendEvent */

enum BinderNfcAidlCloseType {
    BINDER_NFC_AIDL_CLOSE_DISABLE,
    BINDER_NFC_AIDL_CLOSE_HOST_SWITCHED_OFF
};

#define BINDER_NFC_EVENTS(e) \
    e(OPEN_CPLT) \
    e(CLOSE_CPLT) \
    e(POST_INIT_CPLT) \
    e(PRE_DISCOVER_CPLT) \
    e(REQUEST_CONTROL) \
    e(RELEASE_CONTROL) \
    e(ERROR)

#define BINDER_NFC_AIDL_EVENTS(e) \
    e(OPEN_CPLT) \
    e(CLOSE_CPLT) \
    e(POST_INIT_CPLT) \
    e(PRE_DISCOVER_CPLT) \
    e(HCI_NETWORK_RESET) \
    e(ERROR) \
    e(REQUEST_CONTROL) \
    e(RELEASE_CONTROL)

enum BinderNfcEvent {
#define HAL_NFC_EVT(x) HAL_NFC_EVT_##x,
    BINDER_NFC_EVENTS(HAL_NFC_EVT)
#undef HAL_NFC_EVT
};

enum BinderNfcAidlEvent {
#define HAL_NFC_AIDL_EVT(x) HAL_NFC_AIDL_EVT_##x,
    BINDER_NFC_AIDL_EVENTS(HAL_NFC_AIDL_EVT)
#undef HAL_NFC_AIDL_EVT
};

enum BinderNfcStatus_t {
    HAL_NFC_STATUS_OK,
    HAL_NFC_STATUS_FAILED,
    HAL_NFC_STATUS_ERR_TRANSPORT,
    HAL_NFC_STATUS_ERR_CMD_TIMEOUT,
    HAL_NFC_STATUS_REFUSED
};

#define DIR_IN  '>'
#define DIR_OUT '<'

#ifndef DISABLE_HEXDUMP
static
void
binder_hexdump(
    GLogModule* log,
    int level,
    char dir,
    const void* data,
    int len)
{
    const guint8* ptr = data;

    while (len > 0) {
        char buf[GUTIL_HEXDUMP_BUFSIZE];

        guint consumed = gutil_hexdump(buf, ptr, len);
        len -= consumed;
        ptr += consumed;
        gutil_log(log, level, "%c %s", dir, buf);
        dir = ' ';
    }
}

static
void
binder_dump_data(
    char dir,
    const void* data,
    guint len)
{
    const int level = GLOG_LEVEL_VERBOSE;
    GLogModule* log = &binder_hexdump_log;

    if (gutil_log_enabled(log, level)) {
        binder_hexdump(log, level, dir, data, len);
    }
}

    #define BINDER_DUMP(dir, data, len)  binder_dump_data(dir, data, len)
    #define DUMP(f,args...)  gutil_log(&binder_hexdump_log, \
       GLOG_LEVEL_VERBOSE, f, ##args)
#else
    #define BINDER_DUMP(dir, data, len)
    #define DUMP(f,args...)
#endif /* !DISABLE_HEXDUMP */

static
gboolean
binder_nfc_adapter_close(
    BinderNfcAdapter* self);

static
void
binder_nfc_adapter_state_check(
    BinderNfcAdapter* self);

/*==========================================================================*
 * INfcClientCallback
 *==========================================================================*/

static
int
binder_nfc_callback_handle_event(
    BinderNfcAdapter* self,
    GBinderReader* reader)
{
    guint32 event, status;

    if (gbinder_reader_read_uint32(reader, &event) &&
        gbinder_reader_read_uint32(reader, &status) &&
        gbinder_reader_at_end(reader)) {
        BinderNfcAdapterFunc action = NULL;

        if (self->binder_aidl) {
            if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
                switch (event) {
#define HAL_NFC_AIDL_DUMP_EVT(x) case HAL_NFC_AIDL_EVT_##x: GDEBUG("> " #x); break;
                BINDER_NFC_AIDL_EVENTS(HAL_NFC_AIDL_DUMP_EVT)
                default:
                    GDEBUG("> event %u", event);
                    break;
                }
            }
            switch (event) {
            case HAL_NFC_AIDL_EVT_OPEN_CPLT:
                action = self->open_cplt;
                self->open_cplt = NULL;
                break;
            case HAL_NFC_AIDL_EVT_CLOSE_CPLT:
                action = self->close_cplt;
                self->close_cplt = NULL;
                break;
            default:
                break;
            }
        } else {
            if (GLOG_ENABLED(GLOG_LEVEL_DEBUG)) {
                switch (event) {
#define HAL_NFC_DUMP_EVT(x) case HAL_NFC_EVT_##x: GDEBUG("> " #x); break;
                BINDER_NFC_EVENTS(HAL_NFC_DUMP_EVT)
                default:
                    GDEBUG("> event %u", event);
                    break;
                }
            }
            switch (event) {
            case HAL_NFC_EVT_OPEN_CPLT:
                action = self->open_cplt;
                self->open_cplt = NULL;
                break;
            case HAL_NFC_EVT_CLOSE_CPLT:
                action = self->close_cplt;
                self->close_cplt = NULL;
                break;
            default:
                break;
            }
        }
        if (action) {
            action(self);
        }
        return GBINDER_STATUS_OK;
    } else {
        GWARN("Failed to parse INfcClientCallback::sendEvent payload");
        return GBINDER_STATUS_FAILED;
    }
}

static
int
binder_nfc_callback_handle_data(
    BinderNfcAdapter* self,
    GBinderReader* reader)
{
    gsize len;
    const guint8* data;
    if (self->binder_aidl) {
        data = gbinder_reader_read_byte_array(reader, &len);
    } else {
        data = gbinder_reader_read_hidl_byte_vec(reader, &len);
    }

    if (data && gbinder_reader_at_end(reader)) {
        NciHalClient* hal_client = self->hal_client;

        DUMP("%c data, %u byte(s)", DIR_IN, (guint)len);
        BINDER_DUMP(DIR_IN, data, len);
        if (hal_client) {
            hal_client->fn->read(hal_client, data, len);
        }
        return GBINDER_STATUS_OK;
    } else {
        GWARN("Failed to parse INfcClientCallback::sendData payload");
        return GBINDER_STATUS_FAILED;
    }
}

static
GBinderLocalReply*
binder_nfc_callback_handler(
    GBinderLocalObject* obj,
    GBinderRemoteRequest* req,
    guint code,
    guint flags,
    int* status,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    const char* iface = gbinder_remote_request_interface(req);

    if (self->binder_aidl) {
        if (!g_strcmp0(iface, BINDER_NFC_AIDL_CALLBACK)) {
            GBinderReader reader;

            gbinder_remote_request_init_reader(req, &reader);
            switch (code) {
            case BINDER_NFC_AIDL_REQ_CALLBACK_SEND_EVENT:
                GDEBUG(BINDER_NFC_AIDL_CALLBACK " %u sendEvent", code);
                *status = binder_nfc_callback_handle_event(self, &reader);
                break;
            case BINDER_NFC_AIDL_REQ_SEND_DATA:
                GDEBUG(BINDER_NFC_AIDL_CALLBACK " %u sendData", code);
                *status = binder_nfc_callback_handle_data(self, &reader);
                break;
            default:
                GDEBUG(BINDER_NFC_AIDL_CALLBACK " %u", code);
                *status = GBINDER_STATUS_FAILED;
                break;
            }
        } else {
            GDEBUG("%s %u", iface, code);
            *status = GBINDER_STATUS_FAILED;
        }
    } else {
        if (!g_strcmp0(iface, BINDER_NFC_CALLBACK)) {
            GBinderReader reader;

            gbinder_remote_request_init_reader(req, &reader);
            switch (code) {
            case BINDER_NFC_REQ_CALLBACK_SEND_EVENT:
                GDEBUG(BINDER_NFC_CALLBACK " %u sendEvent", code);
                *status = binder_nfc_callback_handle_event(self, &reader);
                break;
            case BINDER_NFC_REQ_SEND_DATA:
                GDEBUG(BINDER_NFC_CALLBACK " %u sendData", code);
                *status = binder_nfc_callback_handle_data(self, &reader);
                break;
            default:
                GDEBUG(BINDER_NFC_CALLBACK " %u", code);
                *status = GBINDER_STATUS_FAILED;
                break;
            }
        } else {
            GDEBUG("%s %u", iface, code);
            *status = GBINDER_STATUS_FAILED;
        }
    }
    return (*status == GBINDER_STATUS_OK) ? gbinder_local_reply_append_int32
        (gbinder_local_object_new_reply(obj), 0) : NULL;
}

/*==========================================================================*
 * INfc
 *==========================================================================*/

static
gulong
binder_nfc_client_open(
    BinderNfcAdapter* self,
    GBinderClientReplyFunc reply)
{
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);
    gulong id;

    GASSERT(self->callback);
    gbinder_local_request_append_local_object(req, self->callback);
    id = gbinder_client_transact(self->client,
        self->binder_aidl ? BINDER_NFC_AIDL_REQ_OPEN : BINDER_NFC_REQ_OPEN,
        0, req, reply, NULL, self);
    gbinder_local_request_unref(req);
    return id;
}

static
gulong
binder_nfc_client_write(
    BinderNfcAdapter* self,
    const void* data,
    gsize len,
    GBinderClientReplyFunc complete,
    GDestroyNotify destroy,
    void* user_data)
{
    GBinderLocalRequest* req = gbinder_client_new_request(self->client);
    GBinderWriter writer;
    gulong id;

    BINDER_DUMP(DIR_OUT, data, len);
    gbinder_local_request_init_writer(req, &writer);
    if (self->binder_aidl) {
        gbinder_writer_append_byte_array(&writer, data, len);
    } else {
        gbinder_writer_append_hidl_vec(&writer, data, len, 1);
    }
    id = gbinder_client_transact(self->client,
        self->binder_aidl ? BINDER_NFC_AIDL_REQ_WRITE : BINDER_NFC_REQ_WRITE,
        0, req, complete, destroy, user_data);
    gbinder_local_request_unref(req);
    return id;
}

static
gulong
binder_nfc_client_close(
    BinderNfcAdapter* self,
    GBinderClientReplyFunc reply)
{
    GBinderLocalRequest* req = NULL;
    gulong id;
    if (self->binder_aidl) {
        req = gbinder_client_new_request(self->client);
        gbinder_local_request_append_int32(req, BINDER_NFC_AIDL_CLOSE_DISABLE);
    }
    id = gbinder_client_transact(self->client,
        self->binder_aidl ? BINDER_NFC_AIDL_REQ_CLOSE : BINDER_NFC_REQ_CLOSE,
        0, req, reply, NULL, self);
    gbinder_local_request_unref(req);
    return id;
}

static
gulong
binder_nfc_client_core_initialized(
    BinderNfcAdapter* self,
    GBinderClientReplyFunc reply)
{
    return gbinder_client_transact(self->client,
        self->binder_aidl ? BINDER_NFC_AIDL_REQ_CORE_INITIALIZED
                          : BINDER_NFC_REQ_CORE_INITIALIZED,
        0, NULL, reply, NULL, self);
}

static
gulong
binder_nfc_client_prediscover(
    BinderNfcAdapter* self,
    GBinderClientReplyFunc reply)
{
    return gbinder_client_transact(self->client,
        self->binder_aidl ? BINDER_NFC_AIDL_REQ_PREDISCOVER
                          : BINDER_NFC_REQ_PREDISCOVER,
        0, NULL, reply, NULL, self);
}

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
binder_nfc_adapter_set_power(
    BinderNfcAdapter* self,
    gboolean on)
{
    NciCore* nci = self->adapter.nci;

    if (self->power_switch_pending) {
        self->power_switch_pending = FALSE;
        self->power_on = on;
        if (on) {
            nci_core_restart(nci);
        }
        nfc_adapter_power_notify(NFC_ADAPTER(self), on, TRUE);
    } else if (self->power_on != on) {
        self->power_on = on;
        if (on) {
            nci_core_restart(nci);
        }
        nfc_adapter_power_notify(NFC_ADAPTER(self), on, FALSE);
    }
}

static
gboolean
binder_nfc_adapter_can_close(
    BinderNfcAdapter* self)
{
    NciCore* nci = self->adapter.nci;

    return (nci->current_state <= NCI_RFST_IDLE);
}

static
void
binder_nfc_adapter_open_done(
    BinderNfcAdapter* self)
{
    GDEBUG("Power on");
    binder_nfc_adapter_set_power(self, TRUE);
}

static
void
binder_nfc_adapter_open_cplt(
    BinderNfcAdapter* self)
{
    if (!self->pending_tx) {
        /* open call already completed */
        binder_nfc_adapter_open_done(self);
    } else {
        GDEBUG("Waiting for open to complete");
    }
}

static
void
binder_nfc_adapter_open_cancel(
    BinderNfcAdapter* self)
{
    binder_nfc_adapter_close(self);
}

static
void
binder_nfc_adapter_open_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    int result = -1;

    GBinderReader reader;
    gbinder_remote_reply_init_reader(reply, &reader);

    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    const gboolean success = (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result) &&
        result == 0);

    GASSERT(self->pending_tx);
    self->pending_tx = 0;
    if (self->need_power) {
        if (success) {
            if (self->open_cplt) {
                GDEBUG("Waiting for OPEN_CPLT");
            } else {
                binder_nfc_adapter_open_done(self);
            }
        } else {
            GWARN("Power on error %d", result);
            self->open_cplt = NULL;
            binder_nfc_adapter_set_power(self, FALSE);
        }
    } else {
        GDEBUG("Opps, we don't need the power anymore");
        if (self->open_cplt) {
            self->open_cplt = binder_nfc_adapter_open_cancel;
        } else {
            binder_nfc_adapter_close(self);
        }
    }
}

static
gboolean
binder_nfc_adapter_open(
    BinderNfcAdapter* self)
{
    GDEBUG("Opening adapter");
    if (!self->callback) {
        GBinderIpc* ipc = gbinder_remote_object_ipc(self->remote);
        static const char* ifaces[] = { BINDER_NFC_CALLBACK, NULL };
        static const char* aidl_ifaces[] = { BINDER_NFC_AIDL_CALLBACK, NULL };

        self->callback = gbinder_local_object_new(ipc,
            self->binder_aidl ? aidl_ifaces : ifaces,
            binder_nfc_callback_handler, self);
        if (self->binder_aidl) {
            gbinder_local_object_set_stability(self->callback,
                GBINDER_STABILITY_VINTF);
        }
    }
    self->core_initialized = FALSE;
    self->open_cplt = binder_nfc_adapter_open_cplt;
    self->pending_tx = binder_nfc_client_open(self,
        binder_nfc_adapter_open_reply);
    return (self->pending_tx != 0);
}

static
void
binder_nfc_adapter_reopen_cplt(
    BinderNfcAdapter* self)
{
    GASSERT(!self->pending_tx);
    self->pending_tx = binder_nfc_adapter_open(self);
}

static
void
binder_nfc_adapter_close_done(
    BinderNfcAdapter* self)
{
    /* We can release our local object now */
    gbinder_local_object_drop(self->callback);
    self->callback = NULL;

    GDEBUG("Power off");
    binder_nfc_adapter_set_power(self, FALSE);
}

static
void
binder_nfc_adapter_close_cplt(
    BinderNfcAdapter* self)
{
    if (!self->pending_tx) {
        /* close call already completed */
        binder_nfc_adapter_close_done(self);
    } else {
        GDEBUG("Waiting for close to complete");
    }
}

static
void
binder_nfc_adapter_close_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    int result = 0;

    GBinderReader reader;
    gbinder_remote_reply_init_reader(reply, &reader);

    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    const gboolean success = (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result) &&
        result == 0);

    GVERIFY(gbinder_remote_reply_read_int32(reply, &result));
    GASSERT(self->pending_tx);
    GASSERT(self->power_on);

    self->pending_tx = 0;
    if (self->need_power) {
        /* Reopen the adapter */
        GDEBUG("Opps, we need the power");
        if (self->close_cplt) {
            self->close_cplt = binder_nfc_adapter_reopen_cplt;
        } else {
            self->pending_tx = binder_nfc_adapter_open(self);
        }
    } else {
        if (success) {
            /*
             * Don't wait for CLOSE_CPLT, it may never come. In those cases
             * when it does come, it usually comes before completion of the
             * close() call.
             */
            self->close_cplt = NULL;
            binder_nfc_adapter_close_done(self);
        } else {
            GWARN("Power on error %d", result);
            self->close_cplt = NULL;
            binder_nfc_adapter_close_done(self);
        }
    }
}

static
gboolean
binder_nfc_adapter_close(
    BinderNfcAdapter* self)
{
    NciCore* nci = self->adapter.nci;

    /* Should never be > NCI_RFST_IDLE but let's check >= just in case */
    if (nci->current_state >= NCI_RFST_IDLE) {
        /*
         * Make sure that state machine isn't going to continue
         * transition to RFST_DISCOVERY while we are closing the
         * adapter.
         */
        nci_core_set_state(nci, NCI_RFST_IDLE);
    }

    GDEBUG("Closing adapter");
    GASSERT(!self->pending_tx);
    self->close_cplt = binder_nfc_adapter_close_cplt;
    self->pending_tx = binder_nfc_client_close(self,
        binder_nfc_adapter_close_reply);
    return (self->pending_tx != 0);
}

static
void
binder_nfc_adapter_power_check(
    BinderNfcAdapter* self)
{
    if (self->power_on && !self->need_power && !self->pending_tx) {
        if (binder_nfc_adapter_can_close(self)) {
            binder_nfc_adapter_close(self);
        }
    }
}

static
void
binder_nfc_adapter_prediscover_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    NciCore* nci = self->adapter.nci;

#if GUTIL_LOG_DEBUG
    int result;

    if (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result)) {
        GDEBUG("PREDISCOVER status %d", result);
    } else {
        GDEBUG("PREDISCOVER status failed (that's ok)");
    }
#endif /* GUTIL_LOG_DEBUG */

    self->pending_tx = 0;
    nci_core_set_state(nci, NCI_RFST_DISCOVERY);
    binder_nfc_adapter_state_check(self);
}

static
void
binder_nfc_adapter_core_initialized_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);

#if GUTIL_LOG_DEBUG
    int result;

    if (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result)) {
        GDEBUG("CORE_INITIALIZED status %d", result);
    } else {
        GDEBUG("CORE_INITIALIZED failed (that's ok)");
    }
#endif /* GUTIL_LOG_DEBUG */

    self->pending_tx = 0;
    binder_nfc_adapter_state_check(self);
}

static
void
binder_nfc_adapter_nci_check(
    BinderNfcAdapter* self)
{
    NciCore* nci = self->adapter.nci;

    if (self->power_on && self->need_power && !self->pending_tx) {
        if (nci->current_state == NCI_RFST_IDLE &&
            nci->next_state == NCI_RFST_IDLE) {
            if (!self->core_initialized) {
                self->core_initialized = TRUE;
                self->pending_tx = binder_nfc_client_core_initialized(self,
                    binder_nfc_adapter_core_initialized_reply);
            } else {
                /* This includes both first time initialization and the case
                 * when NCI state machine has switched to IDLE by itself. */
                self->pending_tx = binder_nfc_client_prediscover(self,
                    binder_nfc_adapter_prediscover_reply);
            }
        }
    }
}

static
void
binder_nfc_adapter_state_check(
    BinderNfcAdapter* self)
{
    binder_nfc_adapter_nci_check(self);
    binder_nfc_adapter_power_check(self);
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcAdapter*
binder_nfc_adapter_new(
    GBinderServiceManager* sm,
    const char* name)
{
    int status = 0;
    char* fqname;
    gboolean binder_aidl = g_str_equal(gbinder_servicemanager_device(sm),
        GBINDER_DEFAULT_BINDER);
    if (binder_aidl) {
        fqname = g_strdup(BINDER_NFC_NAME_AIDL);
    } else {
        fqname = g_strdup(BINDER_NFC_NAME);
    }
    GBinderRemoteObject* remote = gbinder_servicemanager_get_service_sync(sm,
        fqname, &status);

    if (remote) {
        BinderNfcAdapter* self = g_object_new(BINDER_NFC_TYPE_ADAPTER, NULL);

        self->binder_aidl = binder_aidl;
        /* gbinder_servicemanager_get_service_sync() returns auto-released
         * reference, we need to add a reference of our own */
        self->remote = gbinder_remote_object_ref(remote);
        self->client = gbinder_client_new(self->remote,
            self->binder_aidl ? BINDER_NFC_AIDL : BINDER_NFC);
        self->fqname = fqname;
        GDEBUG("Connected to %s", fqname);
        return NFC_ADAPTER(self);
    } else {
        GERR("Failed to connect to %s", fqname);
    }

    g_free(fqname);
    return NULL;
}

static
void
binder_nfc_adapter_death(
    GBinderRemoteObject* remote,
    void* adapter)
{
    g_signal_emit(BINDER_NFC_ADAPTER(adapter),
        binder_nfc_adapter_signals[SIGNAL_DEATH], 0);
}

gulong
binder_nfc_adapter_add_death_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc fn,
    void* data)
{
    if (G_LIKELY(adapter) && G_LIKELY(fn)) {
        BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);

        if (!self->death_id) {
            self->death_id = gbinder_remote_object_add_death_handler
                (self->remote, binder_nfc_adapter_death, self);
        }
        return g_signal_connect(self, SIGNAL_DEATH_NAME, G_CALLBACK(fn), data);
    }
    return 0;
}

/*==========================================================================*
 * Methods
 *==========================================================================*/

static
void
binder_nfc_adapter_current_state_changed(
    NciAdapter* adapter)
{
    NCI_ADAPTER_CLASS(SUPER_CLASS)->current_state_changed(adapter);
    binder_nfc_adapter_state_check(BINDER_NFC_ADAPTER(adapter));
}

static
void
binder_nfc_adapter_next_state_changed(
    NciAdapter* adapter)
{
    NCI_ADAPTER_CLASS(SUPER_CLASS)->next_state_changed(adapter);
    binder_nfc_adapter_state_check(BINDER_NFC_ADAPTER(adapter));
}

static
gboolean
binder_nfc_adapter_submit_power_request(
    NfcAdapter* adapter,
    gboolean on)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);
    NciCore* nci = self->adapter.nci;

    self->need_power = on;
    if (self->pending_tx) {
        GDEBUG("Waiting for pending call to complete");
        self->power_switch_pending = TRUE;
    } else if (on) {
        if (self->power_on) {
            GDEBUG("Adapter already opened");
            nci_core_set_state(nci, NCI_RFST_IDLE);
            /* Power stays on, we are done */
        } else {
            self->power_switch_pending = binder_nfc_adapter_open(self);
        }
    } else {
        if (self->power_on) {
            if (binder_nfc_adapter_can_close(self)) {
                self->power_switch_pending = binder_nfc_adapter_close(self);
            } else {
                GDEBUG("Waiting for NCI state machine to become idle");
                nci_core_set_state(nci, NCI_RFST_IDLE);
                self->power_switch_pending =
                    (nci->current_state != NCI_RFST_IDLE &&
                    nci->next_state == NCI_RFST_IDLE);
            }
        } else {
            GDEBUG("Adapter already closed");
            /* Power stays off, we are done */
        }
    }
    return self->power_switch_pending;
}

static
void
binder_nfc_adapter_cancel_power_request(
    NfcAdapter* adapter)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);

    self->need_power = self->power_on;
    self->power_switch_pending = FALSE;
}

/*==========================================================================*
 * NFC HAL I/O
 *==========================================================================*/

typedef struct binder_nci_write_data {
    BinderNfcAdapter* self;
    NciHalClientFunc complete;
} BinderNciWriteData;

static
BinderNfcAdapter*
binder_nfc_adapter_from_nci_hal_io(
    NciHalIo* hal_io)
{
    return G_CAST(hal_io, BinderNfcAdapter, hal_io);
}

static
void
binder_nci_adapter_hal_io_write_data_free(
    gpointer data)
{
    g_slice_free1(sizeof(BinderNciWriteData), data);
}

static
void
binder_nfc_adapter_hal_io_write_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    gint32 result;
    BinderNciWriteData* write_data = user_data;
    BinderNfcAdapter* self = write_data->self;
    const gboolean success = (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result) &&
        result == 0);

    self->nci_write_id = 0;
    if (write_data->complete) {
        write_data->complete(self->hal_client, success);
    }
}

static
gboolean
binder_nfc_adapter_hal_io_start(
    NciHalIo* hal_io,
    NciHalClient* hal_client)
{
    BinderNfcAdapter* self = binder_nfc_adapter_from_nci_hal_io(hal_io);

    self->hal_client = hal_client;
    return TRUE;
}

static
void
binder_nfc_adapter_hal_io_stop(
    NciHalIo* hal_io)
{
    BinderNfcAdapter* self = binder_nfc_adapter_from_nci_hal_io(hal_io);

    if (self->nci_write_id) {
        gbinder_client_cancel(self->client, self->nci_write_id);
        self->nci_write_id = 0;
    }
    self->hal_client = NULL;
}

static
gboolean
binder_nfc_adapter_hal_io_write(
    NciHalIo* hal_io,
    const GUtilData* chunks,
    guint count,
    NciHalClientFunc complete)
{
    BinderNfcAdapter* self = binder_nfc_adapter_from_nci_hal_io(hal_io);
    guint len = 0;
    const guint8* data = NULL;
    guint8* tmp_buf = NULL;

#pragma message("Add gbinder API to concatenate multiple buffers into one?")

    if (count == 1) {
        data = chunks->bytes;
        len = chunks->size;
    } else {
        guint i;

        for (i = 0; i < count; i++) {
            len += chunks[i].size;
        }

        if (len > 0) {
            guint off = 0;

            data = tmp_buf = g_malloc(len);
            for (i = 0; i < count; i++) {
                memcpy(tmp_buf + off, chunks[i].bytes, chunks[i].size);
                off += chunks[i].size;
            }
        }
    }

    GASSERT(!self->nci_write_id);
    if (data) {
        BinderNciWriteData* write_data = g_slice_new(BinderNciWriteData);

        write_data->self = self;
        write_data->complete = complete;

        self->nci_write_id = binder_nfc_client_write(self, data, len,
            binder_nfc_adapter_hal_io_write_reply,
            binder_nci_adapter_hal_io_write_data_free, write_data);
    }

    g_free(tmp_buf);
    return (self->nci_write_id != 0);
}

static
void
binder_nfc_adapter_hal_io_cancel_write(
    NciHalIo* hal_io)
{
    BinderNfcAdapter* self = binder_nfc_adapter_from_nci_hal_io(hal_io);

    GASSERT(self->nci_write_id);
    gbinder_client_cancel(self->client, self->nci_write_id);
    self->nci_write_id = 0;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
binder_nfc_adapter_init(
    BinderNfcAdapter* self)
{
    static const NciHalIoFunctions hal_io_functions = {
        .start = binder_nfc_adapter_hal_io_start,
        .stop = binder_nfc_adapter_hal_io_stop,
        .write = binder_nfc_adapter_hal_io_write,
        .cancel_write = binder_nfc_adapter_hal_io_cancel_write
    };

    self->hal_io.fn = &hal_io_functions;
    nci_adapter_init_base(&self->adapter, &self->hal_io);
}

static
void
binder_nfc_adapter_finalize(
    GObject* object)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(object);

    gbinder_client_cancel(self->client, self->nci_write_id);
    gbinder_client_cancel(self->client, self->pending_tx);
    gbinder_client_unref(self->client);
    gbinder_local_object_drop(self->callback);
    gbinder_remote_object_remove_handler(self->remote, self->death_id);
    gbinder_remote_object_unref(self->remote);
    g_free(self->fqname);
    G_OBJECT_CLASS(SUPER_CLASS)->finalize(object);
}

static
void
binder_nfc_adapter_class_init(
    BinderNfcAdapterClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    NfcAdapterClass* nfc_adapter_class = NFC_ADAPTER_CLASS(klass);

    klass->current_state_changed = binder_nfc_adapter_current_state_changed;
    klass->next_state_changed = binder_nfc_adapter_next_state_changed;
    nfc_adapter_class->submit_power_request =
        binder_nfc_adapter_submit_power_request;
    nfc_adapter_class->cancel_power_request =
        binder_nfc_adapter_cancel_power_request;
    object_class->finalize = binder_nfc_adapter_finalize;
    binder_nfc_adapter_signals[SIGNAL_DEATH] =
        g_signal_new(SIGNAL_DEATH_NAME, G_OBJECT_CLASS_TYPE(klass),
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
