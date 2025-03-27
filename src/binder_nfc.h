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

#ifndef BINDER_NFC_H
#define BINDER_NFC_H

/* Internal header file for binder plugin implementation */

#define GLOG_MODULE_NAME binder_log
#include <gutil_log.h>

extern GLogModule binder_hexdump_log;

#include <nfc_adapter.h>
#include <nci_types.h>
#include <gbinder_types.h>

typedef struct binder_nfc_target BinderNfcTarget;

typedef
void
(*BinderNfcTargetPresenseCheckFunc)(
    NfcTarget* target,
    gboolean ok,
    void* user_data);

GType binder_nfc_target_get_type(void);
#define BINDER_NFC_TYPE_TARGET (binder_nfc_target_get_type())
#define BINDER_NFC_TARGET(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        BINDER_NFC_TYPE_TARGET, BinderNfcTarget))

#define BINDER_IFACE(x)     "android.hardware.nfc@1.0::" x
#define BINDER_NFC          BINDER_IFACE("INfc")
#define BINDER_NFC_CALLBACK BINDER_IFACE("INfcClientCallback")
#define BINDER_NFC_NAME     BINDER_NFC "/default"

#define BINDER_IFACE_AIDL(x)     "android.hardware.nfc." x
#define BINDER_NFC_AIDL          BINDER_IFACE_AIDL("INfc")
#define BINDER_NFC_AIDL_CALLBACK BINDER_IFACE_AIDL("INfcClientCallback")
#define BINDER_NFC_NAME_AIDL     BINDER_NFC_AIDL "/default"

NfcAdapter*
binder_nfc_adapter_new(
    GBinderServiceManager* sm,
    const char* name);

gulong
binder_nfc_adapter_add_death_handler(
    NfcAdapter* obj,
    NfcAdapterFunc func,
    void* user_data);

#endif /* BINDER_NFC_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
