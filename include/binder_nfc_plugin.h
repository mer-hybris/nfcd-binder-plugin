/*
 * Copyright (C) 2019 Jolla Ltd.
 * Copyright (C) 2019 Slava Monich <slava.monich@jolla.com>
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

#ifndef BINDER_NFC_PLUGIN_H
#define BINDER_NFC_PLUGIN_H

#include "nfc_types.h"

#define BINDER_NFC_PLUGIN_NAME "binder"

G_BEGIN_DECLS

/*
 * BinderNfcPlugin remains blocked until at least one BinderNfcPluginBlock
 * is alive. Note that it may not become blocked immediately when you call
 * binder_nfc_plugin_block_new() function. It may take some time for it to
 * cleanly shutdown NCI state machine.
 *
 * These functions are exported by nfcbinder.so
 */

typedef struct binder_nfc_plugin BinderNfcPlugin;
typedef struct binder_nfc_plugin_block BinderNfcPluginBlock;

typedef
void
(*BinderNfcPluginFunc)(
    BinderNfcPlugin* binder,
    void* user_data);

BinderNfcPlugin*
binder_nfc_plugin_cast(
    NfcPlugin* plugin);

BinderNfcPlugin*
binder_nfc_plugin_ref(
    BinderNfcPlugin* binder);

void
binder_nfc_plugin_unref(
    BinderNfcPlugin* binder);

BinderNfcPluginBlock*
binder_nfc_plugin_block_new(
    BinderNfcPlugin* binder);

void
binder_nfc_plugin_block_free(
    BinderNfcPluginBlock* block);

gboolean
binder_nfc_plugin_is_blocked(
    BinderNfcPlugin* binder);

gulong
binder_nfc_plugin_add_blocked_handler(
    BinderNfcPlugin* binder,
    BinderNfcPluginFunc callback,
    void* user_data);

void
binder_nfc_plugin_remove_handler(
    BinderNfcPlugin* binder,
    gulong id);

G_END_DECLS

#endif /* BINDER_NFC_PLUGIN_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
