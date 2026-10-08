/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef BINDER_NFC_CLOSE_H
#define BINDER_NFC_CLOSE_H

#include <stdbool.h>

typedef struct binder_nfc_close {
    bool pending;
    bool reply_ok;
    bool event_ok;
    bool failed;
} BinderNfcClose;

static inline void binder_nfc_close_begin(BinderNfcClose* close)
{
    close->pending = true;
    close->reply_ok = close->event_ok = false;
    /* An uncertain session cannot become safe by retrying its close. */
}

static inline void binder_nfc_close_reply(BinderNfcClose* close, bool ok)
{
    if (close->pending) {
        close->reply_ok = ok;
        close->failed |= !ok;
    }
}

static inline void binder_nfc_close_event(BinderNfcClose* close, bool ok)
{
    if (close->pending) {
        close->event_ok = ok;
        close->failed |= !ok;
    }
}

static inline bool binder_nfc_close_confirmed(const BinderNfcClose* close)
{
    return close->pending && close->reply_ok && close->event_ok && !close->failed;
}

#endif
