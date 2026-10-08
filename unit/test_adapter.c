/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Exercise the adapter's actual lifecycle with a fake Binder transport. The
 * core mock implements the asynchronous power-request contract; unused adapter
 * entry points are discarded by the linker. No hardware or daemon is needed. */
#define G_DISABLE_CAST_CHECKS
#define DISABLE_HEXDUMP
#include <nci_core.h>
#include <nci_adapter_impl.h>

static void test_nci_restart(NciCore* nci) { }
static void test_nci_set_state(NciCore* nci, int state)
{
    nci->current_state = nci->next_state = state;
}
#define nci_core_restart test_nci_restart
#define nci_core_set_state test_nci_set_state
#include "../src/binder_nfc_adapter.c"

GLogModule binder_log = { .name = "test" };

static struct {
    BinderNfcAdapter adapter;
    NciCore nci;
    gboolean pending;
    gboolean submitted;
    gboolean auto_close;
    gboolean close_ok;
    gboolean event_first;
    guint opens;
    guint closes;
    BinderNfcApiCompleteFunc open_cb;
    BinderNfcApiCompleteFunc close_cb;
    gpointer open_data;
    gpointer close_data;
    char* dir;
} test;

void nfc_adapter_power_notify(NfcAdapter* adapter, gboolean on,
    gboolean requested)
{
    if (requested) test.pending = FALSE;
    adapter->powered = on;
}

void nfc_adapter_request_power(NfcAdapter* adapter, gboolean on)
{
    adapter->power_requested = on;
    on = on && adapter->enabled;
    if ((test.pending && test.submitted != on) ||
        (!test.pending && adapter->powered != on)) {
        if (test.pending) binder_nfc_adapter_cancel_power_request(adapter);
        test.submitted = on;
        test.pending = TRUE;
        if (!binder_nfc_adapter_submit_power_request(adapter, on)) {
            test.pending = FALSE;
        }
    }
}

static void test_close_finish(void)
{
    BinderNfcApiCompleteFunc cb = test.close_cb;
    gpointer data = test.close_data;
    test.close_cb = NULL;
    if (test.close_ok) {
        if (test.event_first) {
            binder_nfc_adapter_handle_event(NULL,
                BINDER_NFC_EVENT_CLOSE_CPLT, data);
        }
        cb(NULL, TRUE, data);
        if (!test.event_first) {
            binder_nfc_adapter_handle_event(NULL,
                BINDER_NFC_EVENT_CLOSE_CPLT, data);
        }
    } else {
        /* A failed close reply does not promise a completion callback. */
        cb(NULL, FALSE, data);
    }
}

static gboolean test_close_idle(gpointer data)
{
    test_close_finish();
    return G_SOURCE_REMOVE;
}

gulong binder_nfc_api_open(BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete, GDestroyNotify destroy, gpointer data)
{
    test.opens++;
    test.open_cb = complete;
    test.open_data = data;
    return 1;
}

gulong binder_nfc_api_close(BinderNfcApi* api,
    BinderNfcApiCompleteFunc complete, GDestroyNotify destroy, gpointer data)
{
    test.closes++;
    test.close_cb = complete;
    test.close_data = data;
    if (test.auto_close) g_idle_add(test_close_idle, NULL);
    return 2;
}

static void test_init_adapter(void)
{
    BinderNfcAdapter* self = &test.adapter;

    memset(&test, 0, sizeof(test));
    test.close_ok = TRUE;
    test.dir = g_dir_make_tmp("nfc-adapter-XXXXXX", NULL);
    g_assert_nonnull(test.dir);
    self->guard_path = g_build_filename(test.dir, "guard", NULL);
    self->boot_id = g_strdup("01234567-89ab-cdef-0123-456789abcdef");
    self->adapter.nci = &test.nci;
    test.nci.current_state = test.nci.next_state = NCI_RFST_IDLE;
    NFC_ADAPTER(self)->enabled = TRUE;
}

static void test_cleanup_adapter(void)
{
    g_unlink(test.adapter.guard_path);
    g_rmdir(test.dir);
    g_free(test.adapter.guard_path);
    g_free(test.adapter.boot_id);
    g_free(test.dir);
}

static void test_open_finish(void)
{
    BinderNfcApiCompleteFunc cb = test.open_cb;
    gpointer data = test.open_data;

    test.open_cb = NULL;
    cb(NULL, TRUE, data);
    binder_nfc_adapter_handle_event(NULL, BINDER_NFC_EVENT_OPEN_CPLT, data);
}

static void test_enable(void)
{
    nfc_adapter_request_power(NFC_ADAPTER(&test.adapter), TRUE);
    test_open_finish();
    g_assert_true(NFC_ADAPTER(&test.adapter)->powered);
    g_assert_false(test.pending);
}

static void test_reopen_block(gconstpointer event_first)
{
    NfcAdapter* adapter = NFC_ADAPTER(&test.adapter);

    test_init_adapter();
    test_enable();
    test.event_first = GPOINTER_TO_INT(event_first);
    nfc_adapter_request_power(adapter, FALSE);
    nfc_adapter_request_power(adapter, TRUE);
    test_close_finish();
    g_assert_cmpuint(test.opens, ==, 2);
    g_assert_true(test.pending);
    /* RequestBlock must still observe busy and submit a new power-off. */
    nfc_adapter_request_power(adapter, FALSE);
    test_open_finish();
    g_assert_cmpuint(test.closes, ==, 2);
    test_close_finish();
    g_assert_false(adapter->powered);
    g_assert_false(test.pending);
    g_assert_false(g_file_test(test.adapter.guard_path, G_FILE_TEST_EXISTS));
    test_cleanup_adapter();
}

static void test_shutdown(gconstpointer event_first)
{
    test_init_adapter();
    test_enable();
    test.auto_close = TRUE;
    test.event_first = GPOINTER_TO_INT(event_first);
    g_assert_true(binder_nfc_adapter_shutdown(NFC_ADAPTER(&test.adapter), 100));
    g_assert_false(g_file_test(test.adapter.guard_path, G_FILE_TEST_EXISTS));
    g_assert_true(binder_nfc_guard_is_clear(test.adapter.guard_path,
        test.adapter.boot_id));
    /* A late request must not reopen an adapter that is being torn down. */
    nfc_adapter_request_power(NFC_ADAPTER(&test.adapter), TRUE);
    g_assert_cmpuint(test.opens, ==, 1);
    test_cleanup_adapter();
}

static gboolean test_open_idle(gpointer data)
{
    test_open_finish();
    return G_SOURCE_REMOVE;
}

static void test_shutdown_open_pending(void)
{
    test_init_adapter();
    nfc_adapter_request_power(NFC_ADAPTER(&test.adapter), TRUE);
    test.auto_close = TRUE;
    g_idle_add(test_open_idle, NULL);
    g_assert_true(binder_nfc_adapter_shutdown(NFC_ADAPTER(&test.adapter), 100));
    g_assert_cmpuint(test.closes, ==, 1);
    g_assert_false(g_file_test(test.adapter.guard_path, G_FILE_TEST_EXISTS));
    test_cleanup_adapter();
}

static void test_shutdown_reply_failed(void)
{
    test_init_adapter();
    test_enable();
    test.auto_close = TRUE;
    test.close_ok = FALSE;
    g_assert_false(binder_nfc_adapter_shutdown(NFC_ADAPTER(&test.adapter), 100));
    g_assert_false(binder_nfc_guard_is_clear(test.adapter.guard_path,
        test.adapter.boot_id));
    test_cleanup_adapter();
}

static void test_shutdown_timeout(void)
{
    test_init_adapter();
    test_enable();
    g_assert_false(binder_nfc_adapter_shutdown(NFC_ADAPTER(&test.adapter), 1));
    g_assert_false(binder_nfc_guard_is_clear(test.adapter.guard_path,
        test.adapter.boot_id));
    test_cleanup_adapter();
}

int main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_data_func("/adapter/reopen_block/reply_first", NULL,
        test_reopen_block);
    g_test_add_data_func("/adapter/reopen_block/event_first", GINT_TO_POINTER(1),
        test_reopen_block);
    g_test_add_func("/adapter/shutdown/open_pending", test_shutdown_open_pending);
    g_test_add_data_func("/adapter/shutdown/reply_first", NULL, test_shutdown);
    g_test_add_data_func("/adapter/shutdown/event_first", GINT_TO_POINTER(1),
        test_shutdown);
    g_test_add_func("/adapter/shutdown/reply_failed", test_shutdown_reply_failed);
    g_test_add_func("/adapter/shutdown/timeout", test_shutdown_timeout);
    return g_test_run();
}
