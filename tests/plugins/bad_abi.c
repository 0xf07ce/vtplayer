/*
 * Dummy plugin advertising an incompatible ABI version.
 *
 * Copyright (c) 2026 Leon J. Lee
 * SPDX-License-Identifier: MIT
 *
 * vtp_register() deliberately succeeds (returns non-NULL) but the manifest
 * claims a future ABI version. The host must independently re-check
 * manifest->abi_version and skip the plugin — proving a version-mismatched
 * plugin is dropped cleanly rather than registering its (".bad") extension.
 */
#include "vtplayer/plugin.h"

#include <stddef.h>

static void *bad_open(const char *path)
{
    (void) path;
    return NULL;
}
static uint32_t bad_read(void *h, float *o, uint32_t f)
{
    (void) h;
    (void) o;
    (void) f;
    return 0;
}
static int bad_seek(void *h, double s)
{
    (void) h;
    (void) s;
    return -1;
}
static double bad_duration(void *h)
{
    (void) h;
    return 0.0;
}
static int bad_seekable(void *h)
{
    (void) h;
    return 0;
}
static void bad_close(void *h)
{
    (void) h;
}

static const char *const kExts[] = {"bad"};

static const VtpInputPlugin kInput = {
    sizeof(VtpInputPlugin),
    kExts,
    1,
    bad_open,
    bad_read,
    bad_seek,
    bad_duration,
    bad_seekable,
    bad_close,
    NULL,
};

static const VtpPluginManifest kManifest = {
    sizeof(VtpPluginManifest),
    999u, /* incompatible ABI — host must skip */
    "bad-abi",
    "0.1.0",
    &kInput,
    NULL,
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host)
{
    (void) host_abi;
    (void) host;
    return &kManifest;
}
