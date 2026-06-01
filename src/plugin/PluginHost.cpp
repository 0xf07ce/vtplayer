// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PluginHost.h"

#include "DecoderRegistry.h"
#include "vtplayer/plugin.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <system_error>

#include <dlfcn.h>

namespace fs = std::filesystem;

namespace vtplayer
{

namespace
{

// Host-side debug flag, shared with the C log callback below. Set once before
// loadAll(); read from any thread a plugin might log on.
std::atomic<bool> g_hostDebug{false};

// ---- VtpHostApi service implementations (C linkage, static lifetime) ----

char const * hostConfigDir()
{
    static std::string const dir = [] {
        char const * home = std::getenv("HOME");
        if (!home || !*home)
            return std::string();
        return (fs::path(home) / ".config" / "vtplayer").string();
    }();
    return dir.empty() ? nullptr : dir.c_str();
}

char const * hostCacheDir()
{
    // XDG_CACHE_HOME, else ~/.cache. Created on first call. Mirrors the path
    // helpers in Config / PlayQueueCache but for transient downloaded media.
    static std::string const dir = [] {
        fs::path base;
        if (char const * xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg)
            base = xdg;
        else if (char const * home = std::getenv("HOME"); home && *home)
            base = fs::path(home) / ".cache";
        if (base.empty())
            return std::string();
        fs::path d = base / "vtplayer";
        std::error_code ec;
        fs::create_directories(d, ec); // best effort
        return d.string();
    }();
    return dir.empty() ? nullptr : dir.c_str();
}

void hostLog(int level, char const * msg)
{
    if (!msg)
        return;
    if (!g_hostDebug.load(std::memory_order_relaxed) && level > VTP_LOG_WARN)
        return; // drop info/debug unless host debug is on
    static std::mutex mtx;
    std::lock_guard<std::mutex> lock(mtx);
    std::fprintf(stderr, "[plugin] %s\n", msg);
    std::fflush(stderr);
}

uint32_t packedHostVersion()
{
#ifdef VTPLAYER_VERSION
    // VTPLAYER_VERSION is "MM.mm.pp"; pack to decimal MMmmpp. Best-effort.
    unsigned a = 0, b = 0, c = 0;
    std::sscanf(VTPLAYER_VERSION, "%u.%u.%u", &a, &b, &c);
    return a * 10000u + b * 100u + c;
#else
    return 0u;
#endif
}

VtpHostApi const & hostApi()
{
    static VtpHostApi const api = [] {
        VtpHostApi a{};
        a.struct_size  = sizeof(VtpHostApi);
        a.host_version = packedHostVersion();
        a.config_dir   = &hostConfigDir;
        a.cache_dir    = &hostCacheDir;
        a.log          = &hostLog;
        return a;
    }();
    return api;
}

void loaderLog(bool debug, char const * fmt, char const * arg)
{
    if (!debug)
        return;
    std::fprintf(stderr, "[plugin] ");
    std::fprintf(stderr, fmt, arg);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

bool isLoadableModule(fs::path const & p)
{
    auto ext = p.extension().string();
    for (auto & c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".so" || ext == ".dylib";
}

} // namespace

PluginHost::~PluginHost()
{
    shutdown();
}

void PluginHost::setDebug(bool debug)
{
    _debug = debug;
    g_hostDebug.store(debug, std::memory_order_relaxed);
}

fs::path PluginHost::defaultDir()
{
    char const * home = std::getenv("HOME");
    if (!home || !*home)
        return {};
    return fs::path(home) / ".config" / "vtplayer" / "plugins";
}

void PluginHost::loadAll()
{
    loadFrom(defaultDir());
}

void PluginHost::loadFrom(fs::path const & dir)
{
    if (dir.empty())
        return;
    std::error_code ec;
    if (!fs::is_directory(dir, ec))
        return;

    for (auto const & entry : fs::directory_iterator(dir, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec))
            continue;
        if (!isLoadableModule(entry.path()))
            continue;
        loadOne(entry.path());
    }
}

void PluginHost::loadOne(fs::path const & file)
{
    std::string const path = file.string();

    void * handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        loaderLog(_debug, "dlopen failed: %s", dlerror());
        return;
    }

    auto reg = reinterpret_cast<VtpRegisterFn>(dlsym(handle, "vtp_register"));
    if (!reg)
    {
        loaderLog(_debug, "no vtp_register symbol: %s", path.c_str());
        dlclose(handle);
        return;
    }

    VtpPluginManifest const * manifest = reg(VTP_PLUGIN_ABI_VERSION, &hostApi());
    if (!manifest)
    {
        loaderLog(_debug, "plugin declined to register: %s", path.c_str());
        dlclose(handle);
        return;
    }

    // abi_version sits at a layout-stable offset (right after struct_size), so
    // it is safe to read even from a manifest built against a different ABI.
    // Gate on it FIRST: a mismatch means the rest of the struct may have a
    // different layout, and dereferencing a relocated field (e.g. `input`)
    // would fault. Skipping here is how a stale plugin is ignored, not crashed.
    if (manifest->abi_version != VTP_PLUGIN_ABI_VERSION)
    {
        loaderLog(_debug, "ABI version mismatch, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }

    // Defense in depth: even at a matching ABI, refuse a manifest too small to
    // contain the fields we are about to read (a truncated or foreign build).
    if (manifest->struct_size < sizeof(VtpPluginManifest))
    {
        loaderLog(_debug, "manifest smaller than expected, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }

    if (!manifest->input)
    {
        loaderLog(_debug, "manifest carries no input plugin, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }
    DecoderRegistry::instance().registerInput(manifest->input);

    loaderLog(_debug, "loaded plugin: %s", manifest->name ? manifest->name : path.c_str());
    _loaded.push_back(Loaded{handle, manifest, path});
}

std::vector<PluginHost::PluginInfo> PluginHost::plugins() const
{
    std::vector<PluginInfo> out;
    out.reserve(_loaded.size());
    for (auto const & l : _loaded)
    {
        PluginInfo info;
        if (l.manifest && l.manifest->name && *l.manifest->name)
            info.name = l.manifest->name;
        else
            info.name = fs::path(l.path).filename().string();

        if (l.manifest && l.manifest->version && *l.manifest->version)
            info.version = l.manifest->version;
        else
            info.version = "0.0.0";

        out.push_back(std::move(info));
    }
    return out;
}

void PluginHost::shutdown()
{
    if (_loaded.empty())
        return;

    // Drop registry references first — they point into module static data.
    DecoderRegistry::instance().clear();

    for (auto it = _loaded.rbegin(); it != _loaded.rend(); ++it)
    {
        if (it->handle)
            dlclose(it->handle);
    }
    _loaded.clear();
}

} // namespace vtplayer
