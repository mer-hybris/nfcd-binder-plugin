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

#ifndef BINDER_NFC_API_IMPL_H
#define BINDER_NFC_API_IMPL_H

#include "binder_nfc_api.h"

typedef
gulong
(*BinderNfcApiApiFunc)(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data);

typedef struct binder_nfc_api_class {
    GObjectClass parent;
    BinderNfcApiApiFunc open;
    BinderNfcApiApiFunc close;
    BinderNfcApiApiFunc core_initialized;
    BinderNfcApiApiFunc prediscover;
    gulong (*write)(
        BinderNfcApi* api,
        const void* data,
        gsize len,
        BinderNfcApiCompleteFunc complete,
        GDestroyNotify destroy,
        gpointer user_data);
} BinderNfcApiClass;

#define BINDER_NFC_TYPE_API binder_nfc_api_get_type()
#define BINDER_NFC_API(obj) G_TYPE_CHECK_INSTANCE_CAST(obj, \
        BINDER_NFC_TYPE_API, BinderNfcApi)
#define BINDER_NFC_API_CLASS(klass) G_TYPE_CHECK_CLASS_CAST((klass), \
        BINDER_NFC_TYPE_API, BinderNfcApiClass)

GType BINDER_NFC_TYPE_API G_GNUC_INTERNAL;

void
binder_nfc_api_init_base(
    BinderNfcApi* api,
    GBinderClient* client,
    GBinderRemoteObject* remote)
    G_GNUC_INTERNAL;

void
binder_nfc_api_emit_event(
    BinderNfcApi* api,
    BINDER_NFC_EVENT event)
    G_GNUC_INTERNAL;

void
binder_nfc_api_emit_data(
    BinderNfcApi* api,
    const void* data,
    gsize size)
    G_GNUC_INTERNAL;

typedef struct binder_nfc_api_call {
    BinderNfcApi* api;
} BinderNfcApiCall;

BinderNfcApiCall*
binder_nfc_api_call_new(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

void
binder_nfc_api_call_complete(
    BinderNfcApiCall* call,
    gboolean ok)
    G_GNUC_INTERNAL;

void
binder_nfc_api_call_destroy(
    gpointer call)
    G_GNUC_INTERNAL;

#endif /* BINDER_NFC_API_IMPL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
