// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "vtplayer/plugin.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct VtpPluginManifestV2
{
    uint32_t              struct_size;
    uint32_t              abi_version;
    const char           *name;
    const char           *version;
    const VtpInputPlugin *input;
} VtpPluginManifestV2;

static const char *const kExts[] = {"legacy"};

static void * legacy_open(const char *path)
{
    (void) path;
    return malloc(1);
}

static uint32_t legacy_read(void *h, float *out, uint32_t frames)
{
    (void) h;
    for (uint32_t i = 0; i < frames * VTP_CHANNELS; ++i)
        out[i] = 0.0f;
    return frames > 16 ? 16 : frames;
}

static int legacy_seek(void *h, double seconds)
{
    (void) h;
    (void) seconds;
    return 0;
}

static double legacy_duration(void *h)
{
    (void) h;
    return 0.001;
}

static int legacy_seekable(void *h)
{
    (void) h;
    return 1;
}

static void legacy_close(void *h)
{
    free(h);
}

static const VtpInputPlugin kInput = {
    sizeof(VtpInputPlugin),
    kExts,
    1,
    legacy_open,
    legacy_read,
    legacy_seek,
    legacy_duration,
    legacy_seekable,
    legacy_close,
    NULL,
};

static const VtpPluginManifestV2 kManifest = {
    sizeof(VtpPluginManifestV2),
    2u,
    "legacy-v2",
    "0.1.0",
    &kInput,
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host)
{
    (void) host;
    if (host_abi != 2u)
        return NULL;
    return (const VtpPluginManifest *)&kManifest;
}
