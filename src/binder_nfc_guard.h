/* Copyright (C) 2026 Jolla Mobile Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef BINDER_NFC_GUARD_H
#define BINDER_NFC_GUARD_H

#include <glib.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

static inline gboolean binder_nfc_boot_id_valid(const char* id)
{
    guint i;
    if (!id || strlen(id) != 36) return FALSE;
    for (i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (id[i] != '-') return FALSE;
        } else if (!g_ascii_isxdigit(id[i])) return FALSE;
    }
    return TRUE;
}

static inline gboolean binder_nfc_guard_clear(const char* path)
{
    return g_unlink(path) == 0 || errno == ENOENT;
}

static inline gboolean binder_nfc_guard_save(const char* path, const char* boot)
{
    return binder_nfc_boot_id_valid(boot) &&
        g_file_set_contents(path, boot, -1, NULL);
}

static inline gboolean binder_nfc_guard_is_clear(const char* path,
    const char* boot)
{
    char* saved = NULL;
    GError* error = NULL;
    gboolean clear = FALSE;

    if (!binder_nfc_boot_id_valid(boot)) return FALSE;
    if (g_file_get_contents(path, &saved, NULL, &error)) {
        /* Only a verifiable host boot change clears an uncertain session. */
        if (binder_nfc_boot_id_valid(saved) && strcmp(saved, boot)) {
            clear = binder_nfc_guard_clear(path);
        }
        g_free(saved);
    } else {
        clear = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        g_clear_error(&error);
    }
    return clear;
}

#endif
