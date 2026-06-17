/*
 * vtplayer plugin ABI — public, permissively-licensed interface.
 *
 * Copyright (c) 2026 Leon J. Lee
 * SPDX-License-Identifier: MIT
 *
 * ===========================================================================
 * WHAT THIS FILE IS
 * ===========================================================================
 * This header is the ONLY contract between the vtplayer host and a dynamically
 * loaded plugin (`.so` / `.dylib`). If your code compiles against this header
 * and follows the rules in the comments, it is a valid plugin — you never need
 * to link against, or even look at, the host's source.
 *
 * It is deliberately a pure C ABI so that:
 *   - plugins can be written in C, C++ or Rust (anything with a C FFI), and
 *   - the host and plugin can be licensed independently. This header is MIT;
 *     the host is LGPL. The host links nothing of the plugin's, and the plugin
 *     links nothing of the host's.
 *
 * Everything that crosses the boundary is plain old data: `const char*`
 * (always UTF-8), fixed-layout structs, `float` sample buffers, and function
 * pointers. C++/STL types (std::string, std::filesystem::path, TrackInfo,
 * ventty::*) NEVER cross the boundary.
 *
 * ===========================================================================
 * HOW A PLUGIN WORKS, END TO END
 * ===========================================================================
 *   1. You build a shared library exporting ONE symbol: `vtp_register`.
 *   2. You drop it into `~/.config/vtplayer/plugins/`.
 *   3. At startup the host `dlopen`s it, calls `vtp_register`, and you hand
 *      back a statically-allocated `VtpPluginManifest` describing your plugin
 *      (or NULL to decline).
 *   4. The manifest carries one or more interface pointers:
 *      - `VtpInputPlugin`: a decode backend that claims file extensions
 *        (e.g. "vgm") and produces audio frames on demand.
 *      - `VtpSummonPlugin`: a search/download provider for Summon Track.
 *   5. The host registers every supported interface whose pointer is non-NULL.
 *
 * That is the whole model. A plugin may expose an input backend, a summon
 * provider, or both.
 *
 * The smallest possible plugin is ~40 lines of C. A complete, copy-pasteable
 * example plus a full prose guide (threading, build flags, install, pitfalls)
 * lives in `docs/plugins.md`. Read that if anything here is unclear.
 *
 * ===========================================================================
 * STABILITY RULES (read before changing THIS file)
 * ===========================================================================
 *   - Every public struct begins with `struct_size` (its own sizeof at build
 *     time). New fields are appended ONLY — never reordered or removed. A
 *     reader checks `struct_size` to learn which fields a counterpart actually
 *     populated, so old and new builds interoperate over the shared prefix.
 *   - `VTP_PLUGIN_ABI_VERSION` is bumped ONLY for a breaking change (a field's
 *     meaning changes, or one is removed). Appending fields normally does NOT
 *     bump it unless a host must actively negotiate the new interface.
 *   - C++/STL types never cross the boundary (restated because it matters).
 */
#ifndef VTPLAYER_PLUGIN_H
#define VTPLAYER_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Marks the single entry point as exported. A plugin is expected to build with
 * hidden default visibility (CMake `C_VISIBILITY_PRESET hidden`) so that
 * `vtp_register` is the ONLY symbol the host can `dlsym` — this attribute keeps
 * it visible despite that. A C++ plugin must additionally wrap the definition
 * in `extern "C"` so the symbol name is not mangled. */
#if defined(_WIN32)
#  define VTP_EXPORT __declspec(dllexport)
#else
#  define VTP_EXPORT __attribute__((visibility("default")))
#endif

/* The ABI version this header describes. Your plugin reports the version it was
 * built against (in the manifest) and should decline hosts it does not match.
 * Bumped ONLY on a breaking change — see the stability rules above.
 *
 * v2: VtpPluginManifest dropped the `VtpPluginKind kind` field and the
 *     `union iface` (and the never-shipped VtpProviderPlugin), exposing the
 *     input backend as a plain `const VtpInputPlugin *input`. That moved the
 *     struct's later fields, so a v1 plugin's manifest would be misread by a v2
 *     host. The bump makes the host reject v1 plugins at the abi_version gate
 *     (a field at a layout-stable offset) instead of dereferencing a relocated
 *     `input` pointer and crashing.
 *
 * v3: VtpPluginManifest appended `const VtpSummonPlugin *summon`. Hosts still
 *     accept v2 input plugins by trying v3 registration first, then v2 as a
 *     legacy fallback. The v2 manifest prefix is unchanged. */
#define VTP_PLUGIN_ABI_VERSION 3u

/* The output contract EVERY input plugin must honor: the samples you write in
 * read() are interleaved float32 (L,R,L,R,...), stereo, at 44100 Hz, with each
 * sample nominally in [-1.0, 1.0]. The rest of the pipeline (mixing, gain,
 * visualizers, the output device) is blind to your source codec precisely
 * because you normalize to this one format. If your source differs (mono, 48k,
 * int16, ...), resample/upmix/convert INSIDE the plugin. */
#define VTP_SAMPLE_RATE 44100
#define VTP_CHANNELS    2

/* Severity levels for the host log callback (see VtpHostApi.log). Lower is more
 * severe, mirroring syslog. INFO/DEBUG are dropped unless vtplayer runs with
 * `--debug`; ERROR/WARN always show. */
typedef enum
{
    VTP_LOG_ERROR = 0,
    VTP_LOG_WARN  = 1,
    VTP_LOG_INFO  = 2,
    VTP_LOG_DEBUG = 3,
} VtpLogLevel;

/* ---- Services the host provides TO the plugin --------------------------- *
 * A pointer to this table is passed into vtp_register(). You may copy the
 * struct if you want to keep it. Any `const char*` the host returns is owned by
 * the host and valid for the whole process lifetime — do not free it.
 *
 * Forward-compatibility: a future host may append fields. Before calling a
 * function pointer added after the version you built against, gate it on
 * `struct_size` (and a NULL check). For every field below — all part of the
 * current ABI — a plain `host->log != NULL` style null-check is sufficient.   */
typedef struct
{
    uint32_t struct_size;  /* sizeof(VtpHostApi) at host build time */
    uint32_t host_version; /* vtplayer version packed as decimal MMmmpp,
                            * e.g. 0.15.0 -> 1500. 0 if the host didn't set it. */

    /* Returns "~/.config/vtplayer" — your persistent state / settings dir.
     * Returns NULL if $HOME is unset. The directory may not exist yet. */
    const char *(*config_dir)(void);

    /* Returns "~/.cache/vtplayer" (honoring $XDG_CACHE_HOME), CREATED on first
     * call. Use it for downloaded media or decode scratch. NULL if no cache
     * base could be resolved. */
    const char *(*cache_dir)(void);

    /* Routes a UTF-8 message into the host log at the given VtpLogLevel. This
     * is the ONLY correct way for a plugin to emit diagnostics — writing to
     * stdout/stderr yourself corrupts the terminal UI. Safe to call from ANY
     * thread. `msg` is copied/consumed synchronously; you keep ownership. */
    void (*log)(int level, const char *msg);
} VtpHostApi;

/* ---- Shared tag payload ------------------------------------------------- *
 * Filled by your optional read_tags() so the library scanner can index your
 * files with real metadata. The host zeroes this struct and sets struct_size
 * before calling you; you fill what you know and leave the rest zero. Char
 * fields are fixed-size, NUL-terminated UTF-8 buffers — write fewer bytes than
 * the capacity and NUL-terminate (e.g. via strncpy/snprintf). An empty string
 * field is treated as "unknown" and the host keeps its own fallback.          */
typedef struct
{
    uint32_t struct_size;
    char     title[256];
    char     artist[256];
    char     album[256];
    char     grouping[128]; /* top-level library tree axis (genre-like bucket) */
    int32_t  track_number;  /* 0 = unknown */
    int32_t  year;          /* 0 = unknown */
    double   duration;      /* seconds; <= 0 means unknown */
    /* Appended in a later revision (append-only, see STABILITY RULES) — the
     * depth-1 library axis in AlbumArtist mode. An older plugin built before
     * this field leaves it zeroed, which the host reads as "unknown". */
    char     album_artist[256];
} VtpTagOut;

/* ---- (A) Input plugin: a new decode backend ----------------------------- *
 * This is the heart of a plugin: it claims file extensions and turns a file
 * into a stream of PCM frames.
 *
 * THREADING & LIFETIME — the host guarantees, and you must rely on, the
 * following. Get this right and the rest is easy:
 *
 *   - read() runs on the REALTIME AUDIO THREAD and MUST NOT BLOCK. No disk or
 *     network I/O, no locks held against slow work, ideally no heap allocation.
 *     Do blocking work in open() (or on a background thread you own) and feed
 *     read() from a buffer. Pure synthesis (chip emulation) has nothing to
 *     worry about. A read() that blocks stutters or freezes playback.
 *
 *   - All calls on ONE handle are serialized by the host. open/read/seek/
 *     duration/seekable/close for a given handle never run concurrently with
 *     each other, so a handle's own state needs NO internal locking. (read()
 *     is still the audio thread — the realtime rule above still applies.)
 *
 *   - The host NEVER unloads (dlclose) your module while a handle from it is
 *     open. Your code pages and static data outlive every handle.
 *
 *   - This whole struct, the `exts` array, its strings, and everything reached
 *     from the manifest MUST have STATIC LIFETIME (valid for the whole
 *     process). Returning stack/temporary data is undefined behavior — the host
 *     keeps the pointers.                                                      */
typedef struct VtpInputPlugin
{
    uint32_t            struct_size; /* sizeof(VtpInputPlugin) */

    /* The file extensions this backend claims: lowercase, NO leading dot,
     * e.g. {"vgm","vgz"}. Static array of `n_exts` UTF-8 strings, static
     * lifetime. The host lowercases/strips-dot defensively, but provide them
     * already normalized. NOTE: a plugin claim WINS over libav's built-in
     * formats, and across plugins the first to claim an extension keeps it. */
    const char *const  *exts;
    size_t              n_exts;

    /* Open `path` (a filesystem path, UTF-8) and return an opaque handle that
     * is passed to every other call below. Return NULL on failure — the host
     * treats that as "cannot play this file". Called on the UI thread; this is
     * where blocking I/O / format probing belongs. */
    void     *(*open)(const char *path);

    /* Fill up to `frames` STEREO frames into `out` and return how many you
     * actually wrote. `out` has room for `frames * VTP_CHANNELS` floats laid
     * out interleaved: out[2*i+0]=left, out[2*i+1]=right, samples in [-1,1].
     *
     * EOF SEMANTICS (important): returning FEWER frames than requested means
     * PERMANENT end-of-stream — the host flags track-end and advances to the
     * next track. There is no "short read, try again later". So: return full
     * blocks until the source is genuinely spent, then one final short (or
     * zero) block. If your data isn't ready yet (slow I/O), you must have
     * buffered ahead — never return a short block just to wait, and never block.
     * May be called with frames == 0; return 0. Runs on the AUDIO thread. */
    uint32_t  (*read)(void *h, float *out, uint32_t frames);

    /* Seek so the next read() starts at `seconds` from the beginning. Return 0
     * on success, non-zero on failure. Clamp negative input to 0. UI thread. */
    int       (*seek)(void *h, double seconds);

    /* Total length of the open source in seconds, or <= 0 if unknown. The host
     * reads this once right after open() to size the transport bar. UI thread. */
    double    (*duration)(void *h);

    /* Return 1 if seek() is meaningful for this handle, else 0. Read once after
     * open(). UI thread. */
    int       (*seekable)(void *h);

    /* Release everything associated with `h`. Called exactly once per
     * successful open(); after it returns the host never touches `h` again.
     * UI thread. */
    void      (*close)(void *h);

    /* OPTIONAL (may be NULL): a handle-free metadata probe for the library
     * scanner. Takes a PATH, not a handle — it is independent of open() and may
     * be called without one. Fill `out` (see VtpTagOut) and return 0 on
     * success, non-zero on failure. If NULL or it fails, the host falls back to
     * the filename stem as the title. Runs on a BACKGROUND scanner thread,
     * possibly while a different file plays — so keep it self-contained and free
     * of unsynchronized global state. (For plugin-claimed files the host asks
     * you here and never runs TagLib on them.) */
    int       (*read_tags)(const char *path, VtpTagOut *out);
} VtpInputPlugin;

/* ---- (B) Summon plugin: search/download provider ------------------------ *
 * Summon providers are used by the FileBrowser's "Summon Track" modal. They
 * search a remote/local catalog and download the selected row into a directory
 * chosen by the host.
 *
 * THREADING & LIFETIME:
 *   - create() is called once while the plugin is loaded. The returned handle
 *     is passed to every other summon call for this provider.
 *   - query() and download() run on a host worker thread, never on the audio
 *     thread. They may block, spawn processes, or perform network I/O.
 *   - cancel() may be called from the UI thread while query()/download() is
 *     running. It should ask the in-flight operation to stop promptly.
 *   - destroy() is called after every worker has joined and before dlclose().
 *
 * STRING OWNERSHIP:
 *   - Request strings are host-owned and valid only for the duration of the
 *     call.
 *   - Result strings returned by query() must remain valid until the next
 *     query(), download(), cancel(), or destroy() on the same handle. The host
 *     copies them before returning to the UI.
 *   - DownloadOut is caller-owned fixed storage; write NUL-terminated strings.
 */

#define VTP_SUMMON_RESULT_DISABLED 1u

typedef struct VtpSummonQueryRequest
{
    uint32_t    struct_size;
    const char *query;
    const char *current_dir;
    uint32_t    max_results;
} VtpSummonQueryRequest;

typedef struct VtpSummonResult
{
    uint32_t    struct_size;
    const char *title;
    const char *channel;
    const char *duration;
    const char *url;
    const char *opaque;
    uint32_t    flags;
} VtpSummonResult;

typedef struct VtpSummonDownloadRequest
{
    uint32_t                 struct_size;
    const char              *current_dir;
    const VtpSummonResult   *selected_result;
} VtpSummonDownloadRequest;

typedef struct VtpSummonDownloadOut
{
    uint32_t struct_size;
    char     output_path[4096];
    int      skipped;
    char     message[512];
} VtpSummonDownloadOut;

typedef struct VtpSummonPlugin
{
    uint32_t    struct_size;
    const char *id;
    const char *label;

    void *(*create)(const VtpHostApi *host);
    void  (*destroy)(void *h);
    void  (*cancel)(void *h);

    /* Fill up to *n_results slots in `results`, then set *n_results to the
     * actual number written. Return 0 on success, non-zero on failure. */
    int   (*query)(void *h,
                   const VtpSummonQueryRequest *request,
                   VtpSummonResult *results,
                   size_t *n_results);

    /* Download the selected result into request->current_dir. Return 0 when
     * the operation completed, including an intentional skip for an existing
     * file (out->skipped = 1). Return non-zero on failure/cancellation. */
    int   (*download)(void *h,
                      const VtpSummonDownloadRequest *request,
                      VtpSummonDownloadOut *out);
} VtpSummonPlugin;

/* ---- Manifest + entry point --------------------------------------------- *
 * The single object you hand back from vtp_register(). Allocate it statically
 * (e.g. a file-scope `static const`) so it outlives the call.                 */
typedef struct VtpPluginManifest
{
    uint32_t              struct_size; /* sizeof(VtpPluginManifest) */
    uint32_t              abi_version; /* set to VTP_PLUGIN_ABI_VERSION */
    const char           *name;        /* "libvgm", ... shown in Help->Plugins;
                                        * NULL -> host uses the file name */
    const char           *version;     /* "0.1.0", free-form; NULL -> "0.0.0" */
    const VtpInputPlugin *input;       /* optional decode backend; when non-NULL
                                        * MUST point to static storage */
    const VtpSummonPlugin *summon;     /* optional summon provider; when non-NULL
                                        * MUST point to static storage */
} VtpPluginManifest;

/*
 * The single symbol the host resolves via dlsym — your plugin's entry point.
 *
 *   host_abi : the ABI version the host speaks. If it is not one you support,
 *              return NULL to decline cleanly. (The host ALSO re-checks
 *              manifest->abi_version itself and skips you on mismatch, so a
 *              stale plugin can never crash a newer host — it just won't load.)
 *   host     : the host service table (VtpHostApi). Valid for the process
 *              lifetime; you may copy it.
 *
 * Return a manifest with STATIC lifetime, or NULL to decline. Called on a
 * single thread, during startup before the UI and audio device come up. A host
 * may call more than once with different ABI versions while negotiating legacy
 * fallback; return NULL for ABIs you do not support.
 *
 * Minimal shape:
 *     VTP_EXPORT const VtpPluginManifest *
 *     vtp_register(uint32_t host_abi, const VtpHostApi *host) {
 *         (void) host;
 *         if (host_abi != VTP_PLUGIN_ABI_VERSION) return NULL;
 *         return &kManifest;   // file-scope `static const`
 *     }
 */
VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host);

/* Convenience function-pointer typedef the host uses for its dlsym cast. Plugin
 * authors can ignore this. */
typedef const VtpPluginManifest *(*VtpRegisterFn)(uint32_t, const VtpHostApi *);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VTPLAYER_PLUGIN_H */
