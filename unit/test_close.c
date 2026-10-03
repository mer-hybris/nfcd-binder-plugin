/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "binder_nfc_close.h"
#include <assert.h>

int main(void)
{
    BinderNfcClose close = { 0 };
    assert(!binder_nfc_close_confirmed(&close));
    binder_nfc_close_begin(&close);
    binder_nfc_close_reply(&close, true);
    assert(!binder_nfc_close_confirmed(&close));
    binder_nfc_close_event(&close, true);
    assert(binder_nfc_close_confirmed(&close));

    binder_nfc_close_begin(&close);
    binder_nfc_close_event(&close, true);
    assert(!binder_nfc_close_confirmed(&close));
    binder_nfc_close_reply(&close, true);
    assert(binder_nfc_close_confirmed(&close));

    binder_nfc_close_begin(&close);
    binder_nfc_close_event(&close, true);
    binder_nfc_close_reply(&close, false);
    assert(!binder_nfc_close_confirmed(&close));
    binder_nfc_close_begin(&close);
    binder_nfc_close_reply(&close, true);
    binder_nfc_close_event(&close, true);
    assert(!binder_nfc_close_confirmed(&close));

    close = (BinderNfcClose) { 0 };
    binder_nfc_close_begin(&close);
    binder_nfc_close_event(&close, false);
    binder_nfc_close_reply(&close, true);
    binder_nfc_close_event(&close, true);
    assert(!binder_nfc_close_confirmed(&close));
    return 0;
}
