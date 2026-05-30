/*
 * vtplayer plugin ABI — public, permissively-licensed interface.
 *
 * Copyright (c) 2026 Leon J. Lee
 * SPDX-License-Identifier: MIT
 *
 * This header is the ONLY contract between the vtplayer host and a dynamically
 * loaded plugin (`.so` / `.dylib`). It is deliberately a pure C ABI so plugins
 * can be written in C, C++ or Rust, and so the host and plugin can be licensed
 * independently. The host links nothing of the plugin's; the plugin links
 * nothing of the host's. Everything crosses the boundary as POD: `const char*`
 * (UTF-8), flat structs, `float` buffers and function pointers.
 *
 * Stability rules (read before changing this file):
 *   - Every public struct begins with `struct_size` (its own sizeof at build
 *     time). New fields are appended ONLY — never reordered or removed. A
 *     reader checks `struct_size` to learn which fields a counterpart actually
 *     populated, so old and new builds interoperate.
 *   - `VTP_PLUGIN_ABI_VERSION` is bumped ONLY for a breaking change (a field's
 *     meaning changes, or one is removed). Appending fields does not bump it.
 *   - C++/STL types (std::string, std::filesystem::path, TrackInfo, ventty::*)
 *     never cross the boundary.
 */
#ifndef VTPLAYER_PLUGIN_H
#define VTPLAYER_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mark the single entry point visible: a plugin is expected to build with
 * hidden default visibility (C_VISIBILITY_PRESET hidden), so vtp_register
 * needs this to remain dlsym-able. */
#if defined(_WIN32)
#  define VTP_EXPORT __declspec(dllexport)
#else
#  define VTP_EXPORT __attribute__((visibility("default")))
#endif

/* Bumped only on a breaking ABI change (see header comment). */
#define VTP_PLUGIN_ABI_VERSION 1u

typedef enum
{
    VTP_PLUGIN_INPUT    = 1, /* decode / sample source */
    VTP_PLUGIN_PROVIDER = 2, /* search / feature provider */
} VtpPluginKind;

/* Output contract every input plugin MUST honor: interleaved float32,
 * stereo, 44100 Hz. The rest of the pipeline (mixing, gain, visualizers)
 * is blind to the original codec because of this. */
#define VTP_SAMPLE_RATE 44100
#define VTP_CHANNELS    2

/* Host log levels (mirror typical syslog ordering, low = more severe). */
typedef enum
{
    VTP_LOG_ERROR = 0,
    VTP_LOG_WARN  = 1,
    VTP_LOG_INFO  = 2,
    VTP_LOG_DEBUG = 3,
} VtpLogLevel;

/* ---- Services the host provides to the plugin --------------------------- *
 * Passed by pointer into vtp_register(); the plugin may copy the struct.
 * Returned strings are owned by the host and valid for the process lifetime.
 * Function pointers may be NULL on older hosts — always null-check against
 * `struct_size` before calling a field added after v1.                       */
typedef struct
{
    uint32_t struct_size;  /* sizeof(VtpHostApi) at host build time */
    uint32_t host_version; /* vtplayer version, packed as MMmmpp (decimal) */

    /* "~/.config/vtplayer" — persistent state. NULL if $HOME is unset. */
    const char *(*config_dir)(void);
    /* "~/.cache/vtplayer" — created on first call. For downloaded media /
     * scratch. NULL if no cache base could be resolved. */
    const char *(*cache_dir)(void);
    /* Route a message into the host log. Safe to call from any thread. */
    void (*log)(int level, const char *msg);
} VtpHostApi;

/* ---- Shared tag payload ------------------------------------------------- */
typedef struct
{
    uint32_t struct_size;
    char     title[256];
    char     artist[256];
    char     album[256];
    char     grouping[128];
    int32_t  track_number;
    int32_t  year;
    double   duration; /* seconds; <= 0 means unknown */
} VtpTagOut;

/* ---- (A) Input plugin: a new decode backend ----------------------------- *
 * read() is called on the AUDIO thread and MUST NOT block on I/O. Pure
 * synthesis (chip emulation) is fine; anything that can stall on the network
 * or disk must be wrapped behind its own buffering and exposed as a stream by
 * the plugin author instead.                                                 */
typedef struct VtpInputPlugin
{
    uint32_t            struct_size;
    const char *const  *exts;   /* owned extensions, lowercase, no dot: {"vgm","vgz",...} */
    size_t              n_exts;

    void     *(*open)(const char *path);                     /* NULL = failure */
    uint32_t  (*read)(void *h, float *out, uint32_t frames); /* frames written (< frames => short/EOF) */
    int       (*seek)(void *h, double seconds);              /* 0 = ok */
    double    (*duration)(void *h);                          /* <= 0 = unknown */
    int       (*seekable)(void *h);                          /* 0 / 1 */
    void      (*close)(void *h);

    /* Optional metadata probe for the library scanner. NULL => the host falls
     * back to the filename stem. Return 0 on success. */
    int       (*read_tags)(const char *path, VtpTagOut *out);
} VtpInputPlugin;

/* ---- (B) Provider plugin: search / fetch feature (e.g. "Yoo") ------------ *
 * The host owns all UI; the plugin only answers two questions on a worker
 * thread. Phase 3 wires this up — declared here so the ABI is complete.      */
typedef struct
{
    uint32_t struct_size;
    char     title[256];    /* list display */
    char     subtitle[256];
    char     id[256];       /* opaque key handed back to resolve() */
    double   duration;      /* <= 0 = unknown */
} VtpResult;

typedef struct
{
    uint32_t  struct_size;
    char      path[1024];       /* cached file path (this OR stream_url) */
    char      stream_url[1024]; /* or a stream URL */
    VtpTagOut tags;
} VtpTrackOut;

typedef void (*VtpProgressCb)(void *user, double fraction, const char *note);

typedef struct
{
    uint32_t    struct_size;
    const char *menu_label; /* "Search YouTube" */

    /* Worker thread. out_results is plugin-owned; returned via free_results. */
    int   (*search)(void *h, const char *query, VtpResult **out_results, size_t *out_count);
    /* Worker thread. Resolve a result id to a playable track (downloading if
     * needed). 0 = ok. cb may be NULL. */
    int   (*resolve)(void *h, const char *id, VtpTrackOut *out, VtpProgressCb cb, void *cb_user);
    void  (*free_results)(void *h, VtpResult *results, size_t count);

    void *(*open_session)(void);  /* optional session handle h; may be NULL */
    void  (*close_session)(void *h);
} VtpProviderPlugin;

/* ---- Manifest + entry point --------------------------------------------- */
typedef struct VtpPluginManifest
{
    uint32_t      struct_size;
    uint32_t      abi_version; /* ABI the plugin was built against */
    VtpPluginKind kind;
    const char   *name;        /* "libvgm", "Yoo", ... */
    const char   *version;     /* "0.1.0" */
    union
    {
        const VtpInputPlugin    *input;
        const VtpProviderPlugin *provider;
    } iface;
} VtpPluginManifest;

/*
 * The single symbol the host resolves via dlsym. The host passes the ABI
 * version it speaks and its service table. The plugin returns a manifest with
 * static lifetime, or NULL to decline (e.g. host_abi it cannot support). The
 * host independently re-checks manifest->abi_version and skips on mismatch.
 */
VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host);

/* Convenience signature for dlsym casts on the host side. */
typedef const VtpPluginManifest *(*VtpRegisterFn)(uint32_t, const VtpHostApi *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VTPLAYER_PLUGIN_H */
