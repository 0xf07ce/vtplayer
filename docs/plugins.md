# Plugin Development

This guide is the authoritative, self-contained reference for writing a
vtplayer plugin. It is written to be read without opening the host source: the
ABI is small and every contract below is exact. If a statement here disagrees
with `include/vtplayer/plugin.h`, the header wins — but they are kept in sync.

> **Audience note.** This document is intended to be precise enough for an
> automated coding agent to implement a correct plugin from scratch. Every
> invariant that the host relies on is stated explicitly. Treat the
> "MUST" / "MUST NOT" items as hard requirements — violating them causes audio
> glitches, crashes, or silent non-loading, not graceful degradation.

## 1. What a plugin is

A vtplayer plugin is a dynamically loaded shared library (`.so` on Linux,
`.so` or `.dylib` on macOS) that the host discovers at startup, `dlopen`s, and
queries through a **pure C ABI**. The entire contract is one header
(`vtplayer/plugin.h`) and one exported symbol (`vtp_register`).

The boundary is pure C POD on purpose:

- Plugins can be written in C, C++, Rust, or anything with a C FFI.
- The host and plugin are **licensed independently**. The header is MIT; the
  host is LGPL. A plugin links nothing of the host's and vice versa.
- Only flat data crosses the boundary: `const char*` (UTF-8), fixed-layout
  structs, `float` buffers, and function pointers. **No C++/STL types**
  (`std::string`, `std::filesystem::path`, `TrackInfo`, `ventty::*`) ever cross.

### Plugin interfaces

There are two plugin interfaces. A plugin may expose either one, or both:

- **Input plugin** — a new decode / sample-source backend that claims one or
  more file extensions and produces audio frames.
- **Summon plugin** — a provider for the Summon Track dialog. It searches a
  catalog/service and downloads the selected result into the current
  FileBrowser directory.

An input plugin lets vtplayer play formats libav cannot — chip-tune / retro
formats (VGM, ROL, SID, ...), trackers, or any custom synthesis — by handing the
host decoded PCM on demand. A summon plugin lets vtplayer discover or acquire
tracks without hardcoding a service into the host UI.

## 2. The contract at a glance

| Item | Value |
|------|-------|
| Header | `vtplayer/plugin.h` (MIT, depends only on `<stddef.h>` / `<stdint.h>`) |
| Exported symbol | `const VtpPluginManifest *vtp_register(uint32_t host_abi, const VtpHostApi *host)` |
| Current ABI version | `VTP_PLUGIN_ABI_VERSION` = **3** |
| Output sample format | interleaved **float32**, **stereo** (`VTP_CHANNELS` = 2), **44100 Hz** (`VTP_SAMPLE_RATE`) |
| Plugin install dir | `~/.config/vtplayer/plugins/` |
| Loadable file extensions | `.so`, `.dylib` |

The host calls `vtp_register` during startup, before the terminal UI and audio
device come up. ABI v3 is tried first; if the plugin returns `NULL`, the host
tries ABI v2 as a legacy fallback for old input plugins. Your plugin returns a
pointer to a **statically allocated** manifest, or `NULL` to decline.

## 3. ABI reference

The header is reproduced and annotated below. Copy `vtplayer/plugin.h` into your
plugin project (it is standalone and MIT-licensed) or add vtplayer's `include/`
to your include path.

### 3.1 Entry point

```c
VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi,
                                                 const VtpHostApi *host);
```

- `VTP_EXPORT` expands to `__attribute__((visibility("default")))` (or
  `__declspec(dllexport)` on Windows). Build with hidden default visibility so
  this is the *only* exported symbol.
- `host_abi` is the ABI version the host speaks. If it is not one you support,
  return `NULL`. The host *also* re-checks `manifest->abi_version` independently
  and skips you on mismatch — so returning a manifest with the wrong
  `abi_version` is a safe no-op, but declining early is cleaner.
- `host` points to the host service table (§3.6). You may copy the struct; the
  pointer and any strings it returns are valid for the process lifetime.
- Return a manifest with **static lifetime**. The host stores the pointer and
  never copies the pointed-to data.

### 3.2 `VtpPluginManifest`

```c
typedef struct VtpPluginManifest
{
    uint32_t              struct_size;  /* sizeof(VtpPluginManifest) at build time */
    uint32_t              abi_version;  /* set to VTP_PLUGIN_ABI_VERSION */
    const char           *name;         /* "libvgm", ... (UTF-8, static lifetime) */
    const char           *version;     /* "0.1.0" (free-form, static lifetime) */
    const VtpInputPlugin  *input;       /* optional decode backend */
    const VtpSummonPlugin *summon;      /* optional Summon Track provider */
} VtpPluginManifest;
```

- `struct_size` MUST be `sizeof(VtpPluginManifest)` as seen by *your* compiler.
- `abi_version` MUST be `VTP_PLUGIN_ABI_VERSION`.
- `name` is shown in the Help → Plugins tab and used in log lines. If `NULL`,
  the host falls back to the filename.
- `version` is informational. If `NULL`, the host shows `"0.0.0"`.
- `input`, when non-NULL, MUST point to a statically allocated
  `VtpInputPlugin`.
- `summon`, when non-NULL, MUST point to a statically allocated
  `VtpSummonPlugin`.
- A manifest with no supported interface (`input == NULL` and
  `summon == NULL`, or only incomplete interfaces) is rejected and unloaded.

### 3.3 `VtpInputPlugin` — the decode backend

```c
typedef struct VtpInputPlugin
{
    uint32_t            struct_size;  /* sizeof(VtpInputPlugin) */
    const char *const  *exts;         /* lowercase, no dot: {"vgm","vgz",...} */
    size_t              n_exts;

    void     *(*open)(const char *path);                     /* NULL = failure */
    uint32_t  (*read)(void *h, float *out, uint32_t frames); /* frames written */
    int       (*seek)(void *h, double seconds);              /* 0 = ok */
    double    (*duration)(void *h);                          /* <= 0 = unknown */
    int       (*seekable)(void *h);                          /* 0 / 1 */
    void      (*close)(void *h);

    int       (*read_tags)(const char *path, VtpTagOut *out); /* optional; 0 = ok */
} VtpInputPlugin;
```

**Extensions (`exts` / `n_exts`).** A static array of `n_exts` UTF-8 strings,
each a bare extension — lowercase, no leading dot (`"vgm"`, not `".VGM"`). The
host normalizes defensively (lowercases, strips a leading dot) but you should
provide them already normalized. The array and its strings MUST have static
lifetime.

> **First claim wins, and a plugin beats the built-ins.** When a file is
> opened, the host checks the plugin registry *before* libav. If your plugin
> claims `wav`, you override libav's built-in WAV handling. Across plugins, the
> first one loaded to claim an extension keeps it; later claims for the same
> extension are silently ignored. Load order is directory-iteration order — do
> not depend on it for overlapping claims.

The function pointers, and the contract for each:

| Fn | Thread | Contract |
|----|--------|----------|
| `open(path)` | UI | Open `path`, return an opaque handle, or `NULL` on failure. The host treats `NULL` as "cannot play this file". All other calls receive this handle. |
| `read(h, out, frames)` | **audio (realtime)** | Write up to `frames` **stereo** frames into `out` and return the number actually written. `out` has room for `frames * VTP_CHANNELS` (`= frames * 2`) floats, interleaved L,R,L,R,…, each in roughly `[-1, 1]`. **MUST NOT block.** See "EOF" below. |
| `seek(h, seconds)` | UI | Seek to `seconds` from the start. Return `0` on success, non-zero on failure. Clamp negative input to 0. |
| `duration(h)` | UI | Total length in seconds, or `<= 0` if unknown. The host reads this once right after `open`. |
| `seekable(h)` | UI | Return `1` if `seek` is meaningful, else `0`. The host reads this once after `open`. |
| `close(h)` | UI | Release everything for `h`. The host calls this exactly once per successful `open`. After `close`, the host never touches `h` again. |
| `read_tags(path, out)` | **scanner (background)** | *Optional* (may be `NULL`). A **handle-free** metadata probe used by the library scanner. See §3.5. |

**EOF semantics (critical).** `read` returning **fewer frames than requested**
is interpreted by the host as **permanent end-of-stream** — it flags track-end
and advances to the next queued track. There is no "try again later" short
read. Consequences:

- A finite source returns full blocks until it is genuinely spent, then returns
  a final short (or zero) block once.
- If your source can stall on I/O (network, slow disk), you **MUST** buffer
  internally and keep returning full blocks; never return a short block just
  because data is not ready yet, and never block inside `read`. Pure synthesis
  (chip emulation) has no such problem.
- `read` may be called with `frames == 0`; return `0`.

### 3.4 `VtpSummonPlugin` — Summon Track provider

```c
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
    uint32_t               struct_size;
    const char            *current_dir;
    const VtpSummonResult *selected_result;
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

    int (*query)(void *h,
                 const VtpSummonQueryRequest *request,
                 VtpSummonResult *results,
                 size_t *n_results);

    int (*download)(void *h,
                    const VtpSummonDownloadRequest *request,
                    VtpSummonDownloadOut *out);
} VtpSummonPlugin;
```

`create(host)` is called once when the plugin loads. Return an opaque provider
handle, or `NULL` to make the host ignore this summon interface. `destroy(h)` is
called before the module is unloaded, after the dialog has joined any worker
thread.

`query` runs on a host worker thread. The host passes the query string, the
current FileBrowser directory, and a maximum result count. It also passes a
host-owned `results` array and `*n_results` capacity; fill up to that many
slots, then set `*n_results` to the number written. Return `0` on success.
Result strings must remain valid until the next `query`, `download`, `cancel`,
or `destroy` call on the same handle; the host copies them immediately.

`download` runs on a host worker thread for the selected row. Write the final
path into `out->output_path` on success. If the target already exists and you
intentionally skip work, return `0` and set `out->skipped = 1`. Return non-zero
for failure or cancellation.

`cancel(h)` may be called from the UI thread while `query` or `download` is
running. It should ask the in-flight operation to stop promptly. For spawned
processes, put them in their own process group and kill the group on cancel.

Use `VTP_SUMMON_RESULT_DISABLED` for a visible result row that should not
download anything (for example, "yt-dlp is not available"). The host displays
the row but ignores Enter on it.

### 3.5 `VtpTagOut` and `read_tags`

```c
typedef struct VtpTagOut
{
    uint32_t struct_size;
    char     title[256];
    char     artist[256];
    char     album[256];
    char     grouping[128];
    int32_t  track_number;
    int32_t  year;
    double   duration;      /* seconds; <= 0 means unknown */
    char     album_artist[256]; /* appended; depth-1 axis in AlbumArtist mode */
} VtpTagOut;
```

`read_tags(path, out)` lets the library scanner index your formats with real
metadata instead of just the filename. Because plugin formats are not TagLib
formats, the host asks you *first* and never runs TagLib on a file your plugin
claims.

- It is a **static probe**: it takes a *path*, not an open handle, and may be
  called without any `open`. Make it self-contained.
- The host sets `out->struct_size` and zeroes the struct before calling. Fill
  the fields you know; leave the rest zero.
- Return `0` on success, non-zero on failure. On failure (or if `read_tags` is
  `NULL`), the host falls back to the filename stem as the title.
- Empty string fields are ignored (the host keeps its fallback). A `duration`
  of `<= 0` is treated as unknown.
- Strings are UTF-8. The host applies Unicode NFC normalization, so you do not
  need to.
- Runs on the **background scanner thread**, possibly while a different track is
  playing through `read` on the audio thread. It shares no handle with playback,
  so just keep it free of unsynchronized global mutable state.

### 3.6 `VtpHostApi` — services the host provides

```c
typedef struct VtpHostApi
{
    uint32_t struct_size;  /* sizeof(VtpHostApi) at host build time */
    uint32_t host_version; /* vtplayer version, packed decimal MMmmpp */

    const char *(*config_dir)(void); /* "~/.config/vtplayer" or NULL */
    const char *(*cache_dir)(void);  /* "~/.cache/vtplayer" or NULL  */
    void (*log)(int level, const char *msg);
} VtpHostApi;
```

- `config_dir()` — persistent state directory, or `NULL` if `$HOME` is unset.
- `cache_dir()` — scratch / downloaded-media directory, created on first call,
  or `NULL` if no cache base resolves. Honors `XDG_CACHE_HOME`.
- `log(level, msg)` — route a UTF-8 message into the host log. Levels are
  `VTP_LOG_ERROR` (0), `VTP_LOG_WARN` (1), `VTP_LOG_INFO` (2), `VTP_LOG_DEBUG`
  (3). `INFO`/`DEBUG` are dropped unless the host runs with `--debug`. Safe to
  call from any thread. **Do not** print to `stdout`/`stderr` yourself — it
  corrupts the TUI. Use `log`.
- `host_version` is the vtplayer version packed as decimal `MMmmpp`
  (e.g. `0.15.0` → `1500`).

**Forward-compatibility pattern.** New fields are only ever *appended* to these
structs, and a never bumped struct keeps its layout. Before calling a function
pointer that may not exist on an older host, check `struct_size`:

```c
if (host->struct_size >= offsetof(VtpHostApi, some_new_fn) + sizeof(void*)
    && host->some_new_fn) {
    host->some_new_fn(...);
}
```

For the fields above (part of the current ABI) a simple `host->log != NULL`
null-check is enough.

## 4. Threading & lifetime contract

These are the invariants the host guarantees, and the ones you must uphold.

**Host guarantees to you:**

- `vtp_register` is called **once**, single-threaded, before audio starts.
- **Calls on a single handle are serialized.** `open`, `seek`, `duration`,
  `seekable`, and `close` for a given handle never run concurrently with `read`
  for that same handle — the host holds an internal mutex across the audio
  callback and the UI-thread transport calls. **You do not need internal
  locking** to protect one handle's state between `read` and `seek`.
- A module is **never `dlclose`d while a source from it is active.** The plugin
  pointer outlives every `PluginSource` the host builds from it. Shutdown order:
  audio stops → registry cleared → modules unloaded.
- A summon provider is destroyed only after the Summon Track dialog has closed
  and joined its worker thread. Shutdown order for summon is:
  dialog closes/joins → provider `destroy` → module unloaded.

**You must guarantee to the host:**

- `read` runs on the **realtime audio thread**. It MUST NOT block on I/O, MUST
  NOT lock against slow operations, and should avoid heap allocation on the hot
  path. Do your file/network I/O in `open` (or a background thread you own) and
  feed `read` from a buffer.
- The manifest, the `VtpInputPlugin`, the `exts` array, and all returned strings
  MUST have **static lifetime** (valid for the whole process).
- Handles are opaque to the host; you own their memory and free it in `close`.
- `read_tags`, if provided, must be safe to call on a background thread with no
  shared handle.
- `query` and `download` may block, but they must honor `cancel` promptly.

## 5. How the host uses your plugin

Understanding the host flow helps you fill the contract correctly.

- **Backend selection.** On playback the host lowercases the file extension and
  looks it up in the plugin registry. A hit builds a `PluginSource` around your
  `VtpInputPlugin` and plays through it; a miss falls back to libav. (So your
  claim on an extension wins over the built-in formats.)
- **Library indexing.** The scanner always collects files with plugin-claimed
  extensions, regardless of any other config, and calls `read_tags` to index
  them. `.pls` playlists are also always collected.
- **File browser.** Plugin extensions are added to the browser's visible/openable
  set, so users can navigate to and open your files.
- **Position & duration.** The host reads `duration` once after `open` and drives
  the transport bar from frames played; an unknown (`<= 0`) duration shows an
  indeterminate position.
- **Gain.** Plugin sources are assumed to carry no ReplayGain tags, so the host
  uses its runtime auto-gain path (targeting roughly −18 dBFS). You just emit
  samples around `[-1, 1]`.
- **Summon Track.** On dialog open, the host lists every loaded
  `VtpSummonPlugin` as a tab using `label`. Each provider gets its own query,
  result list, selection, scroll offset, and status. Enter with no results calls
  `query`; Enter on a normal result calls `download`; Enter on a disabled result
  does nothing.

## 6. Complete example — a minimal input plugin

A pure-synthesis sine generator that owns the `.sine` extension. It compiles as
C with no dependencies and exercises every required entry point. For a real
decoder, do format I/O in `open` and decode/synthesize in `read`; everything
else is structurally identical.

```c
/* myplugin.c — a minimal vtplayer input plugin (MIT-licensable). */
#include "vtplayer/plugin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FREQ_HZ     440.0
#define TOTAL_FRAMES (VTP_SAMPLE_RATE * 3) /* 3 seconds, then EOF */

typedef struct { unsigned long pos; } Handle; /* frames produced so far */

static void *my_open(const char *path)
{
    (void) path;                 /* a real plugin would open `path` here */
    return calloc(1, sizeof(Handle)); /* opaque handle; NULL only on OOM */
}

static uint32_t my_read(void *h_, float *out, uint32_t frames)
{
    Handle *h = (Handle *) h_;
    if (!h) return 0;
    uint32_t produced = 0;
    for (; produced < frames; ++produced) {
        if (h->pos >= TOTAL_FRAMES) break;        /* short read => EOF */
        double t = (double) h->pos / (double) VTP_SAMPLE_RATE;
        float s = (float) (sin(2.0 * M_PI * FREQ_HZ * t) * 0.5);
        out[produced * VTP_CHANNELS + 0] = s;     /* L */
        out[produced * VTP_CHANNELS + 1] = s;     /* R */
        h->pos++;
    }
    return produced; /* < frames on the final block signals end-of-stream */
}

static int my_seek(void *h_, double seconds)
{
    Handle *h = (Handle *) h_;
    if (!h) return -1;
    if (seconds < 0.0) seconds = 0.0;
    h->pos = (unsigned long) (seconds * VTP_SAMPLE_RATE);
    return 0;
}

static double my_duration(void *h_) { (void) h_; return (double) TOTAL_FRAMES / VTP_SAMPLE_RATE; }
static int    my_seekable(void *h_) { (void) h_; return 1; }
static void   my_close(void *h_)    { free(h_); }

static int my_read_tags(const char *path, VtpTagOut *out)
{
    (void) path;
    if (!out) return -1;
    strcpy(out->title,    "Test Tone");
    strcpy(out->artist,   "myplugin");
    strcpy(out->album,    "Examples");
    strcpy(out->grouping, "synth");
    out->track_number = 1;
    out->year         = 2026;
    out->duration     = (double) TOTAL_FRAMES / VTP_SAMPLE_RATE;
    return 0;
}

static const char *const kExts[] = { "sine" };

static const VtpInputPlugin kInput = {
    sizeof(VtpInputPlugin),
    kExts, sizeof(kExts) / sizeof(kExts[0]),
    my_open, my_read, my_seek, my_duration, my_seekable, my_close,
    my_read_tags,
};

static const VtpPluginManifest kManifest = {
    sizeof(VtpPluginManifest),
    VTP_PLUGIN_ABI_VERSION,
    "myplugin",
    "0.1.0",
    &kInput,
    NULL,
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi,
                                                 const VtpHostApi *host)
{
    (void) host;
    if (host_abi != VTP_PLUGIN_ABI_VERSION)
        return NULL;             /* decline hosts we don't understand */
    return &kManifest;
}
```

## 7. Complete example — a minimal summon plugin

This provider returns one synthetic row and pretends the download succeeded.
Real providers usually keep per-provider state in the handle returned by
`create`, populate result strings from that handle, and perform network or
process work inside `query` / `download`.

```c
/* mysummon.c — a minimal vtplayer summon plugin (MIT-licensable). */
#include "vtplayer/plugin.h"

#include <stddef.h>
#include <stdint.h>

static int g_handle;

static void *my_create(const VtpHostApi *host)
{
    (void) host;
    return &g_handle;
}

static void my_destroy(void *h) { (void) h; }
static void my_cancel(void *h) { (void) h; }

static int my_query(void *h,
                    const VtpSummonQueryRequest *request,
                    VtpSummonResult *results,
                    size_t *n_results)
{
    (void) h;
    (void) request;
    if (!results || !n_results || *n_results == 0)
        return -1;

    results[0].struct_size = sizeof(VtpSummonResult);
    results[0].title = "Example Track";
    results[0].channel = "Example";
    results[0].duration = "3:21";
    results[0].url = "https://example.invalid/track";
    results[0].opaque = "";
    results[0].flags = 0;
    *n_results = 1;
    return 0;
}

static int my_download(void *h,
                       const VtpSummonDownloadRequest *request,
                       VtpSummonDownloadOut *out)
{
    (void) h;
    (void) request;
    if (!out) return -1;
    out->skipped = 0;
    out->output_path[0] = '\0';
    out->message[0] = '\0';
    return 0;
}

static const VtpSummonPlugin kSummon = {
    sizeof(VtpSummonPlugin),
    "mysummon",
    "My Summon",
    my_create,
    my_destroy,
    my_cancel,
    my_query,
    my_download,
};

static const VtpPluginManifest kManifest = {
    sizeof(VtpPluginManifest),
    VTP_PLUGIN_ABI_VERSION,
    "mysummon",
    "0.1.0",
    NULL,
    &kSummon,
};

VTP_EXPORT const VtpPluginManifest *vtp_register(uint32_t host_abi,
                                                 const VtpHostApi *host)
{
    (void) host;
    if (host_abi != VTP_PLUGIN_ABI_VERSION)
        return NULL;
    return &kManifest;
}
```

## 8. Building the plugin

The plugin needs `vtplayer/plugin.h` on its include path and must be built as a
shared **module** with **hidden visibility** (so `vtp_register` is the only
exported symbol) and position-independent code.

### CMake (recommended)

```cmake
cmake_minimum_required(VERSION 3.20)
project(vtp_myplugin C)

add_library(myplugin MODULE myplugin.c)

set_target_properties(myplugin PROPERTIES
    PREFIX ""                       # output "myplugin.so", not "libmyplugin.so"
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)

# Point at vtplayer's include/ (or a vendored copy of plugin.h).
target_include_directories(myplugin PRIVATE /path/to/vtplayer/include)
```

`add_library(... MODULE ...)` produces a `dlopen`-able file with a `.so` suffix
on both Linux and macOS; the host accepts `.so` and `.dylib`. (Mirrors how the
in-tree test plugins under `tests/plugins/` are built.)

### Raw compiler

```bash
# Linux
cc -std=c11 -O2 -fPIC -fvisibility=hidden \
   -I/path/to/vtplayer/include \
   -shared myplugin.c -o myplugin.so

# macOS
cc -std=c11 -O2 -fPIC -fvisibility=hidden \
   -I/path/to/vtplayer/include \
   -dynamiclib myplugin.c -o myplugin.dylib
```

A C++ or Rust plugin is fine too — just export `extern "C"` `vtp_register` and
keep the boundary POD.

## 9. Installing & loading

1. Copy the built module into the plugin directory:

   ```bash
   mkdir -p ~/.config/vtplayer/plugins
   cp myplugin.so ~/.config/vtplayer/plugins/
   ```

   The host loads every `.so` / `.dylib` directly under that directory at
   startup (non-recursive). A missing directory simply means no plugins.

2. Run with `--debug` to see loader diagnostics on stderr:

   ```bash
   vtplayer --debug
   ```

   On success you get `[plugin] loaded plugin: myplugin`. On rejection you get a
   reason (`dlopen failed`, `no vtp_register symbol`, `ABI version mismatch`,
   `manifest carries no supported interface`).

3. Confirm in the UI: open **Help** and switch to the **Plugins** tab — your
   plugin's `name` and `version` are listed there.

4. Play a file with your extension, or let the library scan index it.

## 10. Verification checklist

Before shipping, confirm:

- [ ] `vtp_register` is the only exported symbol (built with hidden visibility).
- [ ] `vtp_register` returns `NULL` when `host_abi != VTP_PLUGIN_ABI_VERSION`.
- [ ] Every `struct_size` field equals the `sizeof` of its struct.
- [ ] At least one manifest interface pointer is non-NULL and points to static
      storage.
- [ ] For input plugins: `exts` are lowercase, no dot, static lifetime;
      `n_exts` matches.
- [ ] For input plugins: `open` returns `NULL` on failure and a non-NULL handle
      on success.
- [ ] For input plugins: `read` writes interleaved stereo float32, returns frame
      count, and only
      ever returns a short count at true end-of-stream. It never blocks.
- [ ] For input plugins: `read` is allocation-free / lock-free on the hot path.
- [ ] For input plugins: `close` frees the handle and is called exactly once per
      successful `open`.
- [ ] For input plugins: `seek` returns 0 on success; `seekable` reflects
      reality.
- [ ] For input plugins: `read_tags` (if present) is handle-free and
      thread-safe.
- [ ] For summon plugins: `query` fills no more than `*n_results` slots and
      keeps result strings valid until the next provider call.
- [ ] For summon plugins: `download` writes `out->output_path` on success and
      uses `out->skipped = 1` for intentional existing-file skips.
- [ ] For summon plugins: `cancel` stops in-flight work promptly.
- [ ] No writes to `stdout`/`stderr` — use `host->log`.
- [ ] `--debug` shows `loaded plugin: <name>`; the Plugins tab lists it; the
      input path plays a test file or the summon path appears in Summon Track.

## 11. ABI stability rules

- The header begins every struct with `struct_size` and only ever **appends**
  fields — never reorders or removes them. A reader uses `struct_size` to learn
  which fields the other side actually populated, so a newer host and an older
  plugin (or vice versa) interoperate for the shared prefix.
- `VTP_PLUGIN_ABI_VERSION` is bumped **only** on a breaking change, or when the
  host must negotiate a new top-level interface. ABI v3 appended
  `VtpPluginManifest::summon`.
- ABI v3 hosts still load ABI v2 input plugins. The host calls
  `vtp_register(3, ...)` first, then `vtp_register(2, ...)` if the first call
  returns `NULL`. A v2 manifest has no summon field and simply exposes no
  Summon Track provider.
- Pin your plugin to the ABI version it was built against and decline
  unsupported hosts; the host independently re-checks and skips on mismatch, so
  a stale plugin can never crash a newer host — it just won't load.

## 12. Common pitfalls

- **Blocking in `read`.** The single most common mistake. `read` is realtime;
  buffer your I/O elsewhere. A blocked `read` stutters or freezes audio.
- **Short reads for buffering.** A short read means EOF, not "come back later".
  Always return full blocks until truly done.
- **Non-static lifetime.** Returning a manifest/`VtpInputPlugin`/`exts` built on
  the stack or freed later is undefined behavior — the host keeps the pointer.
- **Printing to the terminal.** `printf`/`fprintf(stderr, …)` corrupts the TUI.
  Route everything through `host->log`.
- **Wrong channel/rate.** Output must be stereo, 44100 Hz, interleaved float32.
  Resample/upmix inside the plugin if your source differs.
- **Forgetting `PREFIX ""`.** Without it CMake emits `libmyplugin.so`; that
  still loads (the host accepts any `.so`/`.dylib`), but the conventional output
  name is `myplugin.so`.
