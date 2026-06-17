// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "vtplayer/plugin.h"

#include <stddef.h>
#include <stdint.h>

static int g_handle;

static void * summon_create(const VtpHostApi *host)
{
    (void) host;
    return &g_handle;
}

static void summon_destroy(void *h)
{
    (void) h;
}

static void summon_cancel(void *h)
{
    (void) h;
}

static int summon_query(void *h,
                        const VtpSummonQueryRequest *request,
                        VtpSummonResult *results,
                        size_t *n_results)
{
    (void) h;
    (void) request;
    if (!results || !n_results || *n_results == 0)
        return -1;

    results[0].struct_size = sizeof(VtpSummonResult);
    results[0].title = "Summoned Test Track";
    results[0].channel = "vtplayer";
    results[0].duration = "0:01";
    results[0].url = "https://example.test/track";
    results[0].opaque = "opaque-id";
    results[0].flags = 0;
    *n_results = 1;
    return 0;
}

static int summon_download(void *h,
                           const VtpSummonDownloadRequest *request,
                           VtpSummonDownloadOut *out)
{
    (void) h;
    (void) request;
    if (!out)
        return -1;
    out->skipped = 0;
    out->output_path[0] = '\0';
    out->message[0] = '\0';
    return 0;
}

static const VtpSummonPlugin kSummon = {
    sizeof(VtpSummonPlugin),
    "dummy-summon",
    "Dummy Summon",
    summon_create,
    summon_destroy,
    summon_cancel,
    summon_query,
    summon_download,
};

static const VtpPluginManifest kManifest = {
    sizeof(VtpPluginManifest),
    VTP_PLUGIN_ABI_VERSION,
    "dummy-summon",
    "0.1.0",
    NULL,
    &kSummon,
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host)
{
    (void) host;
    if (host_abi != VTP_PLUGIN_ABI_VERSION)
        return NULL;
    return &kManifest;
}
