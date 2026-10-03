/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Exercise the real HIDL close completion with controlled wire replies. */
#define G_DISABLE_CAST_CHECKS
#include <gbinder.h>
#include "binder_nfc_close.h"

typedef struct test_reply {
    gint32 words[2];
    guint count;
} TestReply;

static const TestReply* test_reply;
static guint test_offset;
static BinderNfcClose test_close;
static gboolean test_result;

static void test_init_reader(GBinderRemoteReply* reply, GBinderReader* reader)
{
    test_reply = (const TestReply*)reply;
    test_offset = 0;
}

static gboolean test_read_int32(GBinderReader* reader, gint32* value)
{
    if (test_reply && test_offset < test_reply->count) {
        *value = test_reply->words[test_offset++];
        return TRUE;
    }
    return FALSE;
}

#define gbinder_remote_reply_init_reader test_init_reader
#define gbinder_reader_read_int32 test_read_int32
#include "../src/binder_nfc_api_hidl.c"

void binder_nfc_api_emit_event(BinderNfcApi* api, BINDER_NFC_EVENT event)
{
    binder_nfc_close_event(&test_close, event == BINDER_NFC_EVENT_CLOSE_CPLT);
}

void binder_nfc_api_call_complete(BinderNfcApiCall* call, gboolean ok)
{
    test_result = ok;
    binder_nfc_close_reply(&test_close, ok);
}

static void test_complete(const TestReply* reply, int status, gboolean expected)
{
    BinderNfcApiCall call = { NULL };

    test_close = (BinderNfcClose) { 0 };
    binder_nfc_close_begin(&test_close);
    test_result = !expected;
    /* No HAL callback is emitted: only this synchronous reply is delivered. */
    binder_nfc_api_hidl_close_complete(NULL, (GBinderRemoteReply*)reply,
        status, &call);
    g_assert_cmpint(test_result, ==, expected);
    g_assert_cmpint(binder_nfc_close_confirmed(&test_close), ==, expected);
}

static void test_success(void)
{
    const TestReply reply = { { 0, 0 }, 2 };
    test_complete(&reply, GBINDER_STATUS_OK, TRUE);
}

static void test_hal_failure(void)
{
    const TestReply reply = { { 0, 1 }, 2 };
    test_complete(&reply, GBINDER_STATUS_OK, FALSE);
}

static void test_exception(void)
{
    const TestReply reply = { { -1, 0 }, 2 };
    test_complete(&reply, GBINDER_STATUS_OK, FALSE);
}

static void test_transport_failure(void)
{
    const TestReply reply = { { 0, 0 }, 2 };
    test_complete(&reply, GBINDER_STATUS_FAILED, FALSE);
}

static void test_truncated(void)
{
    const TestReply reply = { { 0 }, 1 };
    test_complete(&reply, GBINDER_STATUS_OK, FALSE);
}

static void test_empty(void)
{
    test_complete(NULL, GBINDER_STATUS_OK, FALSE);
}

int main(int argc, char** argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hidl/close/no_callback", test_success);
    g_test_add_func("/hidl/close/hal_failure", test_hal_failure);
    g_test_add_func("/hidl/close/exception", test_exception);
    g_test_add_func("/hidl/close/transport_failure", test_transport_failure);
    g_test_add_func("/hidl/close/truncated", test_truncated);
    g_test_add_func("/hidl/close/empty", test_empty);
    return g_test_run();
}
