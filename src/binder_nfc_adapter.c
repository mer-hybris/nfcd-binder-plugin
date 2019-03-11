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

#include "binder_nfc.h"

#include <nfc_adapter_impl.h>
#include <nfc_target_impl.h>
#include <nfc_tag_t2.h>

#include <nci_core.h>
#include <nci_hal.h>

#include <gbinder.h>

#include <gutil_idlequeue.h>
#include <gutil_idlepool.h>
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

/* Idle queue tags */
enum {
    IDLE_MODE_CHECK
};

/* NCI core events */
enum {
    CORE_EVENT_CURRENT_STATE,
    CORE_EVENT_NEXT_STATE,
    CORE_EVENT_INTF_ACTIVATED,
    CORE_EVENT_COUNT
};

#define PRESENCE_CHECK_PERIOD_MS (250)

typedef struct binder_nfc_adapter BinderNfcAdapter;
typedef NfcAdapterClass BinderNfcAdapterClass;

typedef
void
(*BinderNfcAdapterFunc)(
    BinderNfcAdapter* self);

struct binder_nfc_adapter {
    NfcAdapter adapter;
    GBinderRemoteObject* remote;
    GBinderClient* client;
    GBinderLocalObject* callback;
    NciCore* nci;
    gulong nci_event_id[CORE_EVENT_COUNT];
    NciHalIo hal_io;
    NciHalClient* hal_client;
    gulong nci_write_id;
    NfcTarget* target;
    char* fqname;
    GUtilIdleQueue* idle;
    GUtilIdlePool* pool;
    gboolean core_initialized;
    gulong death_id;

    gboolean need_power;
    gboolean power_on;
    gboolean power_switch_pending;
    gulong pending_tx;
    BinderNfcAdapterFunc open_cplt;
    BinderNfcAdapterFunc close_cplt;

    NCI_STATE desired_state;
    NFC_MODE current_mode;
    gboolean mode_change_pending;

    guint presence_check_id;
    guint presence_check_timer;
};

G_DEFINE_TYPE(BinderNfcAdapter, binder_nfc_adapter, NFC_TYPE_ADAPTER)
#define BINDER_NFC_TYPE_ADAPTER (binder_nfc_adapter_get_type())
#define BINDER_NFC_ADAPTER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        BINDER_NFC_TYPE_ADAPTER, BinderNfcAdapter))

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

#define BINDER_NFC_EVENTS(e) \
    e(OPEN_CPLT) \
    e(CLOSE_CPLT) \
    e(POST_INIT_CPLT) \
    e(PRE_DISCOVER_CPLT) \
    e(REQUEST_CONTROL) \
    e(RELEASE_CONTROL) \
    e(ERROR)

enum BinderNfcEvent {
#define HAL_NFC_EVT(x) HAL_NFC_EVT_##x,
    BINDER_NFC_EVENTS(HAL_NFC_EVT)
#undef HAL_NFC_EVT
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
#define DUMP(f,args...)  gutil_log(&binder_hexdump_log, \
       GLOG_LEVEL_VERBOSE, f, ##args)

static
gboolean
binder_nfc_adapter_close(
    BinderNfcAdapter* self);

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

static
void
binder_nfc_adapter_drop_target(
    BinderNfcAdapter* self)
{
    NfcTarget* target = self->target;

    if (target) {
        self->target = NULL;
        if (self->presence_check_timer) {
            g_source_remove(self->presence_check_timer);
            self->presence_check_timer = 0;
        }
        if (self->presence_check_id) {
            nci_core_cancel(self->nci, self->presence_check_id);
            self->presence_check_id = 0;
        }
        GINFO("Target is gone");
        nfc_target_gone(target);
        nfc_target_unref(target);
    }
}

static
void
binder_nfc_adapter_presence_check_done(
    NfcTarget* target,
    gboolean ok,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);

    GDEBUG("Presence check %s", ok ? "ok" : "failed");
    self->presence_check_id = 0;
    if (!ok) {
        nci_core_set_state(self->nci, NCI_RFST_DISCOVERY);
    }
}

static
gboolean
binder_nfc_adapter_presence_check_timer(
    gpointer user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);

    if (!self->presence_check_id && !self->target->sequence) {
        BinderNfcTarget* target = BINDER_NFC_TARGET(self->target);

        self->presence_check_id = binder_nfc_target_presence_check(target,
            binder_nfc_adapter_presence_check_done, self);
        if (!self->presence_check_id) {
            GDEBUG("Failed to start presence check");
            self->presence_check_timer = 0;
            nci_core_set_state(self->nci, NCI_RFST_DISCOVERY);
            return G_SOURCE_REMOVE;
        }
    } else {
        GDEBUG("Skipped presence check");
    }
    return G_SOURCE_CONTINUE;
}

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
    const guint8* data = gbinder_reader_read_hidl_byte_vec(reader, &len);

    if (data && gbinder_reader_at_end(reader)) {
        NciHalClient* hal_client = self->hal_client;

        DUMP("%c data, %u byte(s)", DIR_IN, (guint)len);
        binder_dump_data(DIR_IN, data, len);
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
    id = gbinder_client_transact(self->client, BINDER_NFC_REQ_OPEN,
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

    binder_dump_data(DIR_OUT, data, len);
    gbinder_local_request_init_writer(req, &writer);
    gbinder_writer_append_hidl_vec(&writer, data, len, 1);
    id = gbinder_client_transact(self->client, BINDER_NFC_REQ_WRITE,
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
    return gbinder_client_transact(self->client, BINDER_NFC_REQ_CLOSE,
        0, NULL, reply, NULL, self);
}

static
gulong
binder_nfc_client_power_cycle(
    BinderNfcAdapter* self,
    GBinderClientReplyFunc reply)
{
    return gbinder_client_transact(self->client, BINDER_NFC_REQ_POWER_CYCLE,
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
    if (self->power_switch_pending) {
        self->power_switch_pending = FALSE;
        self->power_on = on;
        nfc_adapter_power_notify(&self->adapter, on, TRUE);
    } else if (self->power_on != on) {
        self->power_on = on;
        nfc_adapter_power_notify(&self->adapter, on, FALSE);
    }
}

static
gboolean
binder_nfc_adapter_can_close(
    BinderNfcAdapter* self)
{
    return (self->nci->current_state <= NCI_RFST_IDLE);
}

static
void
binder_nfc_adapter_open_done(
    BinderNfcAdapter* self)
{
    GDEBUG("Power on");
    nci_core_restart(self->nci);
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
void
binder_nfc_adapter_power_cycle_reply(
    GBinderClient* client,
    GBinderRemoteReply* reply,
    int status,
    void* user_data)
{
    int result = -1;
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    const gboolean success = (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result) &&
        result == 0);

    GASSERT(self->pending_tx);
    self->pending_tx = 0;
    if (self->need_power) {
        if (success) {
            if (self->open_cplt) {
                GDEBUG("Power cycle done, opening the device");
                self->pending_tx = binder_nfc_client_open(self,
                    binder_nfc_adapter_open_reply);
            } else {
                binder_nfc_adapter_open_done(self);
            }
        } else {
            GWARN("Power cycle error %d, trying to open", result);
            self->open_cplt = binder_nfc_adapter_open_cplt;
            self->pending_tx = binder_nfc_client_open(self,
                binder_nfc_adapter_open_reply);
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

        self->callback = gbinder_local_object_new(ipc, ifaces,
            binder_nfc_callback_handler, self);
    }
    self->core_initialized = FALSE;
    self->open_cplt = binder_nfc_adapter_open_cplt;
    self->pending_tx = binder_nfc_client_power_cycle(self,
        binder_nfc_adapter_power_cycle_reply);
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
    gbinder_local_object_unref(self->callback);
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
binder_nfc_adapter_mode_check(
    BinderNfcAdapter* self)
{
    NciCore* nci = self->nci;
    const NFC_MODE mode = (nci->current_state > NCI_RFST_IDLE) ?
        NFC_MODE_READER_WRITER : NFC_MODE_NONE;

    if (self->mode_change_pending) {
        if (nci->current_state == self->desired_state) {
            self->mode_change_pending = FALSE;
            self->current_mode = mode;
            nfc_adapter_mode_notify(&self->adapter, mode, TRUE);
        }
    } else if (self->current_mode != mode) {
        self->current_mode = mode;
        nfc_adapter_mode_notify(&self->adapter, mode, FALSE);
    }
}

static
void
binder_nfc_adapter_mode_check1(
    void* self)
{
    binder_nfc_adapter_mode_check(BINDER_NFC_ADAPTER(self));
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

#if GUTIL_LOG_DEBUG
    int result;

    if (status == GBINDER_STATUS_OK &&
        gbinder_remote_reply_read_int32(reply, &result)) {
        GDEBUG("PREDISCOVER status %d", result);
    } else {
        GDEBUG("PREDISCOVER status failed");
    }
#endif /* GUTIL_LOG_DEBUG */

    nci_core_set_state(self->nci, NCI_RFST_DISCOVERY);
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
        GDEBUG("CORE_INITIALIZED failed");
    }
#endif /* GUTIL_LOG_DEBUG */

    binder_nfc_adapter_power_check(self);
    binder_nfc_adapter_mode_check(self);
    gbinder_client_transact(self->client, BINDER_NFC_REQ_PREDISCOVER, 0, NULL,
        binder_nfc_adapter_prediscover_reply, NULL, self);
}

static
void
binder_nfc_adapter_maybe_drop_target(
    BinderNfcAdapter* self)
{
    NciCore* nci = self->nci;

    if (nci->current_state != NCI_RFST_POLL_ACTIVE ||
        nci->next_state != NCI_RFST_POLL_ACTIVE) {
        binder_nfc_adapter_drop_target(self);
    }
}

static
void
binder_nfc_adapter_nci_next_state_changed(
    NciCore* nci,
    void* user_data)
{
    binder_nfc_adapter_maybe_drop_target(BINDER_NFC_ADAPTER(user_data));
}

static
void
binder_nfc_adapter_nci_current_state_changed(
    NciCore* nci,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);

    binder_nfc_adapter_maybe_drop_target(self);
    if (nci->current_state == NCI_RFST_IDLE && !self->core_initialized) {
        self->core_initialized = TRUE;
        gbinder_client_transact(self->client,
            BINDER_NFC_REQ_CORE_INITIALIZED, 0, NULL,
            binder_nfc_adapter_core_initialized_reply, NULL, self);
    } else {
        binder_nfc_adapter_power_check(self);
        binder_nfc_adapter_mode_check(self);
    }
}

static
const NfcParamPollA*
binder_nfc_adapter_convert_poll_a(
    NfcParamPollA* dest,
    const NciModeParamPollA* src)
{
    dest->sel_res = src->sel_res;
    dest->nfcid1.bytes = src->nfcid1;
    dest->nfcid1.size = src->nfcid1_len;
    return dest;
}

#ifdef NFC_TAG_T4_H
static
const NfcParamIsoDepPollA*
binder_nfc_adapter_convert_iso_dep_poll_a(
    NfcParamIsoDepPollA* dest,
    const NciActivationParamIsoDepPollA* src)
{
    dest->fsc = src->fsc;
    dest->t1 = src->t1;
    return dest;
}
#endif

static
void
binder_nfc_adapter_nci_intf_activated(
    NciCore* nci,
    const NciIntfActivationNtf* ntf,
    void* user_data)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(user_data);
    const NciModeParam* mp = ntf->mode_param;
    NfcTag* tag = NULL;

    /* Drop the previous target, if any */
    binder_nfc_adapter_drop_target(self);

    /* Register the new tag */
    self->target = binder_nfc_target_new(self->remote, ntf, nci);

    /* Figure out what kind of target we are dealing with */
    if (mp) {
        NfcParamPollA poll_a;

        switch (ntf->mode) {
        case NCI_MODE_PASSIVE_POLL_A:
        case NCI_MODE_ACTIVE_POLL_A:
            switch (ntf->rf_intf) {
            case NCI_RF_INTERFACE_FRAME:
                /* Type 2 Tag */
                tag = nfc_adapter_add_tag_t2(&self->adapter, self->target,
                    binder_nfc_adapter_convert_poll_a(&poll_a, &mp->poll_a));
                break;
            case NCI_RF_INTERFACE_ISO_DEP:
#ifdef NFC_TAG_T4_H
                /* ISO-DEP Type 4A */
                if (ntf->activation_param) {
                    const NciActivationParam* ap = ntf->activation_param;
                    NfcParamIsoDepPollA iso_dep_poll_a;

                    tag = nfc_adapter_add_tag_t4a(&self->adapter, self->target,
                        binder_nfc_adapter_convert_poll_a(&poll_a, &mp->poll_a),
                        binder_nfc_adapter_convert_iso_dep_poll_a
                            (&iso_dep_poll_a, &ap->iso_dep_poll_a));
                }
#endif
                break;
            case NCI_RF_INTERFACE_NFCEE_DIRECT:
            case NCI_RF_INTERFACE_NFC_DEP:
                break;
            }
            break;
        case NCI_MODE_PASSIVE_POLL_B:
        case NCI_MODE_PASSIVE_POLL_F:
        case NCI_MODE_ACTIVE_POLL_F:
        case NCI_MODE_PASSIVE_POLL_15693:
        case NCI_MODE_PASSIVE_LISTEN_A:
        case NCI_MODE_PASSIVE_LISTEN_B:
        case NCI_MODE_PASSIVE_LISTEN_F:
        case NCI_MODE_ACTIVE_LISTEN_A:
        case NCI_MODE_ACTIVE_LISTEN_F:
        case NCI_MODE_PASSIVE_LISTEN_15693:
            break;
        }
    }

    if (!tag) {
        nfc_adapter_add_other_tag(&self->adapter, self->target);
    }

    /* Start periodic presence checks */
    self->presence_check_timer = g_timeout_add(PRESENCE_CHECK_PERIOD_MS,
        binder_nfc_adapter_presence_check_timer, self);
}

static
void
binder_nfc_adapter_schedule_mode_check(
    BinderNfcAdapter* self)
{
    if (!gutil_idle_queue_contains_tag(self->idle, IDLE_MODE_CHECK)) {
        gutil_idle_queue_add_tag(self->idle, IDLE_MODE_CHECK,
            binder_nfc_adapter_mode_check1, self);
    }
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
    char* fqname = g_strconcat(BINDER_NFC "/", name, NULL);
    GBinderRemoteObject* remote = gbinder_servicemanager_get_service_sync(sm,
        fqname, &status);

    if (remote) {
        BinderNfcAdapter* self = g_object_new(BINDER_NFC_TYPE_ADAPTER, NULL);

        /* gbinder_servicemanager_get_service_sync() returns auto-released
         * reference, we need to add a reference of our own */
        self->remote = gbinder_remote_object_ref(remote);
        self->client = gbinder_client_new(self->remote, BINDER_NFC);
        self->fqname = fqname;
        GDEBUG("Connected to %s", fqname);
        return &self->adapter;
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
gboolean
binder_nfc_adapter_submit_power_request(
    NfcAdapter* adapter,
    gboolean on)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);

    self->need_power = on;
    if (self->pending_tx) {
        GDEBUG("Waiting for pending call to complete");
        self->power_switch_pending = TRUE;
    } else if (on) {
        if (self->power_on) {
            GDEBUG("Adapter already opened");
            nci_core_set_state(self->nci, NCI_RFST_IDLE);
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
                self->power_switch_pending = nci_core_set_state(self->nci,
                    NCI_RFST_IDLE);
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

static
gboolean
binder_nfc_adapter_submit_mode_request(
    NfcAdapter* adapter,
    NFC_MODE mode)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);

    self->mode_change_pending = nci_core_set_state(self->nci, NCI_RFST_IDLE);
    if (self->mode_change_pending) {
        self->desired_state = NCI_RFST_IDLE;
        binder_nfc_adapter_schedule_mode_check(self);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
binder_nfc_adapter_cancel_mode_request(
    NfcAdapter* adapter)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(adapter);

    self->mode_change_pending = FALSE;
    binder_nfc_adapter_schedule_mode_check(self);
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
    NfcAdapter* adapter = &self->adapter;

    static const NciHalIoFunctions hal_io_functions = {
        .start = binder_nfc_adapter_hal_io_start,
        .stop = binder_nfc_adapter_hal_io_stop,
        .write = binder_nfc_adapter_hal_io_write,
        .cancel_write = binder_nfc_adapter_hal_io_cancel_write
    };

    self->hal_io.fn = &hal_io_functions;
    self->idle = gutil_idle_queue_new();
    self->pool = gutil_idle_pool_new();

    self->nci = nci_core_new(&self->hal_io);
    self->nci_event_id[CORE_EVENT_CURRENT_STATE] =
        nci_core_add_current_state_changed_handler(self->nci,
            binder_nfc_adapter_nci_current_state_changed, self);
    self->nci_event_id[CORE_EVENT_NEXT_STATE] =
        nci_core_add_next_state_changed_handler(self->nci,
            binder_nfc_adapter_nci_next_state_changed, self);
    self->nci_event_id[CORE_EVENT_INTF_ACTIVATED] =
        nci_core_add_intf_activated_handler(self->nci,
            binder_nfc_adapter_nci_intf_activated, self);

    adapter->supported_modes = NFC_MODE_READER_WRITER;
    adapter->supported_tags = NFC_TAG_TYPE_FELICA |
        NFC_TAG_TYPE_MIFARE_CLASSIC | NFC_TAG_TYPE_MIFARE_ULTRALIGHT;
    adapter->supported_protocols =  NFC_PROTOCOL_T2_TAG
#ifdef NFC_TAG_T4_H
        | NFC_PROTOCOL_T4A_TAG
        | NFC_PROTOCOL_T4B_TAG
        | NFC_PROTOCOL_NFC_DEP
#endif
        ;
}

static
void
binder_nfc_adapter_dispose(
    GObject* object)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(object);

    binder_nfc_adapter_drop_target(self);
    G_OBJECT_CLASS(binder_nfc_adapter_parent_class)->dispose(object);
}

static
void
binder_nfc_adapter_finalize(
    GObject* object)
{
    BinderNfcAdapter* self = BINDER_NFC_ADAPTER(object);

    nci_core_remove_all_handlers(self->nci, self->nci_event_id);
    nci_core_free(self->nci);
    gutil_idle_pool_destroy(self->pool);
    gutil_idle_queue_unref(self->idle);
    gbinder_client_cancel(self->client, self->pending_tx);
    gbinder_client_unref(self->client);
    gbinder_local_object_drop(self->callback);
    gbinder_remote_object_remove_handler(self->remote, self->death_id);
    gbinder_remote_object_unref(self->remote);
    g_free(self->fqname);
    G_OBJECT_CLASS(binder_nfc_adapter_parent_class)->finalize(object);
}

static
void
binder_nfc_adapter_class_init(
    NfcAdapterClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);

    klass->submit_power_request = binder_nfc_adapter_submit_power_request;
    klass->cancel_power_request = binder_nfc_adapter_cancel_power_request;
    klass->submit_mode_request = binder_nfc_adapter_submit_mode_request;
    klass->cancel_mode_request = binder_nfc_adapter_cancel_mode_request;
    object_class->dispose = binder_nfc_adapter_dispose;
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
