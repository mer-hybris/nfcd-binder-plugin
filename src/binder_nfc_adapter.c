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

#include "binder_nfc_adapter.h"
#include "binder_nfc_api.h"

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
    BinderNfcApi* api;
    NciHalIo hal_io;
    NciHalClient* hal_client;
    gulong nci_write_id;
    gulong death_id;
    gulong event_id;
    gulong data_id;

    gboolean core_initialized;
    gboolean need_power;
    gboolean power_on;
    gboolean power_switch_pending;
    gulong pending_tx;
    BinderNfcAdapterFunc open_cplt;
    BinderNfcAdapterFunc close_cplt;
};

#define PARENT_CLASS binder_nfc_adapter_parent_class
#define PARENT_TYPE NCI_TYPE_ADAPTER
#define THIS_TYPE binder_nfc_adapter_get_type()
#define THIS(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, THIS_TYPE, BinderNfcAdapter)

GType THIS_TYPE G_GNUC_INTERNAL;
G_DEFINE_TYPE(BinderNfcAdapter, binder_nfc_adapter, PARENT_TYPE)

enum binder_nfc_adapter_signal {
    SIGNAL_DEATH,
    SIGNAL_COUNT
};

#define SIGNAL_DEATH_NAME "binder-nfc-adapter-death"

static guint binder_nfc_adapter_signals[SIGNAL_COUNT] = { 0 };

static
gboolean
binder_nfc_adapter_close(
    BinderNfcAdapter* self);

static
void
binder_nfc_adapter_state_check(
    BinderNfcAdapter* self);

/*==========================================================================*
 *  Trace
 *==========================================================================*/

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

/*==========================================================================*
 *  Implementation
 *==========================================================================*/

static
void
binder_nfc_adapter_handle_event(
    BinderNfcApi* api,
    BINDER_NFC_EVENT event,
    gpointer user_data)
{
    BinderNfcAdapter* self = THIS(user_data);
    BinderNfcAdapterFunc action = NULL;

    switch (event) {
    case BINDER_NFC_EVENT_OPEN_CPLT:
        action = self->open_cplt;
        self->open_cplt = NULL;
        break;
    case BINDER_NFC_EVENT_CLOSE_CPLT:
        action = self->close_cplt;
        self->close_cplt = NULL;
        break;
    default:
        break;
    }

    if (action) {
        action(self);
    }
}

static
void
binder_nfc_adapter_handle_data(
    BinderNfcApi* api,
    const void* data,
    gsize size,
    gpointer user_data)
{
    BinderNfcAdapter* self = THIS(user_data);
    NciHalClient* hal_client = self->hal_client;

    DUMP("%c data, %u byte(s)", DIR_IN, (guint) size);
    BINDER_DUMP(DIR_IN, data, size);
    if (hal_client) {
        hal_client->fn->read(hal_client, data, size);
    }
}

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
binder_nfc_adapter_open_complete(
    BinderNfcApi* api,
    gboolean success,
    void* user_data)
{
    BinderNfcAdapter* self = THIS(user_data);

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
            GWARN("Power on error");
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
    self->core_initialized = FALSE;
    self->open_cplt = binder_nfc_adapter_open_cplt;
    self->pending_tx = binder_nfc_api_open(self->api,
        binder_nfc_adapter_open_complete, NULL, self);
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
binder_nfc_adapter_close_complete(
    BinderNfcApi* api,
    gboolean success,
    void* user_data)
{
    BinderNfcAdapter* self = THIS(user_data);

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
            GWARN("Power on error");
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
    self->pending_tx = binder_nfc_api_close(self->api,
        binder_nfc_adapter_close_complete, NULL, self);
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
binder_nfc_adapter_prediscover_complete(
    BinderNfcApi* api,
    gboolean ok,
    void* user_data)
{
    BinderNfcAdapter* self = THIS(user_data);
    NciCore* nci = self->adapter.nci;

    GDEBUG("PREDISCOVER %s", ok ? "ok" : "failed (that's ok)");
    self->pending_tx = 0;
    nci_core_set_state(nci, NCI_RFST_DISCOVERY);
    binder_nfc_adapter_state_check(self);
}

static
void
binder_nfc_adapter_core_initialized_complete(
    BinderNfcApi* api,
    gboolean ok,
    void* user_data)
{
    BinderNfcAdapter* self = THIS(user_data);

    GDEBUG("CORE_INITIALIZED %s", ok ? "ok" : "failed (that's ok)");
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
                self->pending_tx = binder_nfc_api_core_initialized(self->api,
                    binder_nfc_adapter_core_initialized_complete, NULL, self);
            } else {
                /* This includes both first time initialization and the case
                 * when NCI state machine has switched to IDLE by itself. */
                self->pending_tx = binder_nfc_api_prediscover(self->api,
                    binder_nfc_adapter_prediscover_complete, NULL, self);
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

static
void
binder_nfc_adapter_death(
    GBinderRemoteObject* remote,
    void* self)
{
    g_signal_emit(THIS(self), binder_nfc_adapter_signals[SIGNAL_DEATH], 0);
}

/*==========================================================================*
 * Internal API
 *==========================================================================*/

NfcAdapter*
binder_nfc_adapter_new(
    BinderNfcApi* api)
{
    BinderNfcAdapter* self = g_object_new(THIS_TYPE, NULL);

    g_object_ref(self->api = api);
    self->event_id = binder_nfc_api_add_event_handler(api,
        BINDER_NFC_EVENT_ANY, binder_nfc_adapter_handle_event, self);
    self->data_id = binder_nfc_api_add_data_handler(api,
        binder_nfc_adapter_handle_data, self);
    return NFC_ADAPTER(self);
}

gulong
binder_nfc_adapter_add_death_handler(
    NfcAdapter* adapter,
    NfcAdapterFunc fn,
    void* data)
{
    if (G_LIKELY(adapter) && G_LIKELY(fn)) {
        BinderNfcAdapter* self = THIS(adapter);

        if (!self->death_id) {
            self->death_id = gbinder_remote_object_add_death_handler
                (self->api->remote, binder_nfc_adapter_death, self);
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
    NCI_ADAPTER_CLASS(PARENT_CLASS)->current_state_changed(adapter);
    binder_nfc_adapter_state_check(THIS(adapter));
}

static
void
binder_nfc_adapter_next_state_changed(
    NciAdapter* adapter)
{
    NCI_ADAPTER_CLASS(PARENT_CLASS)->next_state_changed(adapter);
    binder_nfc_adapter_state_check(THIS(adapter));
}

static
gboolean
binder_nfc_adapter_submit_power_request(
    NfcAdapter* adapter,
    gboolean on)
{
    BinderNfcAdapter* self = THIS(adapter);
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
    BinderNfcAdapter* self = THIS(adapter);

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
binder_nfc_adapter_hal_io_write_complete(
    BinderNfcApi* api,
    gboolean success,
    void* user_data)
{
    BinderNciWriteData* write_data = user_data;
    BinderNfcAdapter* self = write_data->self;

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
        gbinder_client_cancel(self->api->client, self->nci_write_id);
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

        BINDER_DUMP(DIR_OUT, data, len);
        self->nci_write_id = binder_nfc_api_write(self->api, data, len,
            binder_nfc_adapter_hal_io_write_complete,
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
    gbinder_client_cancel(self->api->client, self->nci_write_id);
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
    BinderNfcAdapter* self = THIS(object);
    BinderNfcApi* api = self->api;

    gbinder_remote_object_remove_handler(api->remote, self->death_id);
    gbinder_client_cancel(api->client, self->nci_write_id);
    gbinder_client_cancel(api->client, self->pending_tx);
    g_signal_handler_disconnect(api, self->event_id);
    g_signal_handler_disconnect(api, self->data_id);
    g_object_unref(api);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
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
