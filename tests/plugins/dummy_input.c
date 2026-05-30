/*
 * Dummy input plugin for vtplayer's plugin loader tests.
 *
 * Copyright (c) 2026 Leon J. Lee
 * SPDX-License-Identifier: MIT
 *
 * A pure-synthesis sine generator owning the ".sine" extension. It exists only
 * to exercise the host loader / DecoderRegistry / PluginSource end to end
 * without any real codec dependency. Written in C to prove the boundary is a
 * genuine C ABI.
 */
#include "vtplayer/plugin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SINE_FREQ    440.0
#define SINE_FRAMES  (VTP_SAMPLE_RATE) /* exactly 1 second of audio, then EOF */

typedef struct
{
    unsigned long pos; /* frames produced so far */
} SineHandle;

static void *sine_open(const char *path)
{
    (void) path;
    SineHandle *h = (SineHandle *) calloc(1, sizeof(SineHandle));
    return h; /* never NULL unless OOM */
}

static uint32_t sine_read(void *handle, float *out, uint32_t frames)
{
    SineHandle *h = (SineHandle *) handle;
    if (!h)
        return 0;
    uint32_t produced = 0;
    for (; produced < frames; ++produced)
    {
        if (h->pos >= SINE_FRAMES)
            break; /* short read => EOF, exactly like Decoder */
        double t = (double) h->pos / (double) VTP_SAMPLE_RATE;
        float s = (float) sin(2.0 * M_PI * SINE_FREQ * t) * 0.5f;
        out[produced * VTP_CHANNELS + 0] = s;
        out[produced * VTP_CHANNELS + 1] = s;
        h->pos++;
    }
    return produced;
}

static int sine_seek(void *handle, double seconds)
{
    SineHandle *h = (SineHandle *) handle;
    if (!h)
        return -1;
    if (seconds < 0.0)
        seconds = 0.0;
    h->pos = (unsigned long) (seconds * VTP_SAMPLE_RATE);
    return 0;
}

static double sine_duration(void *handle)
{
    (void) handle;
    return (double) SINE_FRAMES / (double) VTP_SAMPLE_RATE; /* 1.0s */
}

static int sine_seekable(void *handle)
{
    (void) handle;
    return 1;
}

static void sine_close(void *handle)
{
    free(handle);
}

static int sine_read_tags(const char *path, VtpTagOut *out)
{
    (void) path;
    if (!out)
        return -1;
    strcpy(out->title, "Sine Test Tone");
    strcpy(out->artist, "vtplayer");
    strcpy(out->album, "Plugin Tests");
    strcpy(out->grouping, "test");
    out->track_number = 1;
    out->year = 2026;
    out->duration = (double) SINE_FRAMES / (double) VTP_SAMPLE_RATE;
    return 0;
}

static const char *const kExts[] = {"sine"};

static const VtpInputPlugin kInput = {
    sizeof(VtpInputPlugin),
    kExts,
    1,
    sine_open,
    sine_read,
    sine_seek,
    sine_duration,
    sine_seekable,
    sine_close,
    sine_read_tags,
};

static const VtpPluginManifest kManifest = {
    sizeof(VtpPluginManifest),
    VTP_PLUGIN_ABI_VERSION,
    VTP_PLUGIN_INPUT,
    "dummy-sine",
    "0.1.0",
    {&kInput},
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host)
{
    (void) host;
    if (host_abi != VTP_PLUGIN_ABI_VERSION)
        return NULL; /* decline hosts we don't understand */
    return &kManifest;
}
