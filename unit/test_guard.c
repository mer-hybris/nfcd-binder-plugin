/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "binder_nfc_guard.h"
#include <assert.h>

int main(void)
{
    const char* boot = "01234567-89ab-cdef-0123-456789abcdef";
    const char* next = "11234567-89ab-cdef-0123-456789abcdef";
    char* dir = g_dir_make_tmp("nfc-guard-XXXXXX", NULL);
    char* path;

    assert(dir);
    path = g_build_filename(dir, "guard", NULL);
    assert(binder_nfc_guard_is_clear(path, boot));
    assert(!binder_nfc_guard_save(path, "invalid"));
    assert(binder_nfc_guard_save(path, boot));
    assert(!binder_nfc_guard_is_clear(path, boot));
    assert(!binder_nfc_guard_is_clear(path, NULL));
    assert(binder_nfc_guard_is_clear(path, next));
    assert(!g_file_test(path, G_FILE_TEST_EXISTS));
    assert(g_file_set_contents(path, "invalid", -1, NULL));
    assert(!binder_nfc_guard_is_clear(path, boot));
    assert(binder_nfc_guard_clear(path));
    assert(binder_nfc_guard_clear(path));
    g_free(path);
    assert(!g_rmdir(dir));
    g_free(dir);
    return 0;
}
