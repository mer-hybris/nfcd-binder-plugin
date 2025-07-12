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

#ifndef BINDER_NFC_API_H
#define BINDER_NFC_API_H

#include "binder_nfc_types.h"

#include <glib-object.h>

/* Abstract binder NFC API */

struct binder_nfc_api {
    GObject object;
    GBinderClient* client;
    GBinderRemoteObject* remote;
};

typedef enum binder_nfc_event {
    BINDER_NFC_EVENT_ANY,
    BINDER_NFC_EVENT_OPEN_CPLT,
    BINDER_NFC_EVENT_CLOSE_CPLT
} BINDER_NFC_EVENT;

typedef
void
(*BinderNfcApiCompleteFunc)(
    BinderNfcApi* api,
    gboolean ok,
    gpointer user_data);

typedef
void
(*BinderNfcApiEventFunc)(
    BinderNfcApi* api,
    BINDER_NFC_EVENT event,
    gpointer user_data);

typedef
void
(*BinderNfcApiDataFunc)(
    BinderNfcApi* api,
    const void* data,
    gsize size,
    gpointer user_data);

gulong
binder_nfc_api_open(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_close(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_core_initialized(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_prediscover(
    BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_write(
    BinderNfcApi* api,
    const void* data,
    gsize len,
    BinderNfcApiCompleteFunc complete,
    GDestroyNotify destroy,
    gpointer user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_add_event_handler(
    BinderNfcApi* api,
    BINDER_NFC_EVENT event,
    BinderNfcApiEventFunc func,
    void* user_data)
    G_GNUC_INTERNAL;

gulong
binder_nfc_api_add_data_handler(
    BinderNfcApi* api,
    BinderNfcApiDataFunc func,
    void* user_data)
    G_GNUC_INTERNAL;

#endif /* BINDER_NFC_API_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
