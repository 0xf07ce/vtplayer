// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PluginHost.h"

#include "DecoderRegistry.h"
#include "vtplayer/plugin.h"

#include <atomic>
#include <cstddef>
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
constexpr uint32_t kLegacyAbiVersion = 2u;

#define VTP_FIELD_END(type, field) (offsetof(type, field) + sizeof(((type *)nullptr)->field))

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

bool manifestHasInput(VtpPluginManifest const * manifest)
{
    return manifest
           && manifest->struct_size >= VTP_FIELD_END(VtpPluginManifest, input)
           && manifest->input != nullptr;
}

bool manifestHasSummon(VtpPluginManifest const * manifest)
{
    return manifest
           && manifest->abi_version >= VTP_PLUGIN_ABI_VERSION
           && manifest->struct_size >= VTP_FIELD_END(VtpPluginManifest, summon)
           && manifest->summon != nullptr;
}

std::string manifestName(VtpPluginManifest const * manifest, std::string const & path)
{
    if (manifest && manifest->struct_size >= VTP_FIELD_END(VtpPluginManifest, name)
        && manifest->name && *manifest->name)
    {
        return manifest->name;
    }
    return fs::path(path).filename().string();
}

std::string manifestVersion(VtpPluginManifest const * manifest)
{
    if (manifest && manifest->struct_size >= VTP_FIELD_END(VtpPluginManifest, version)
        && manifest->version && *manifest->version)
    {
        return manifest->version;
    }
    return "0.0.0";
}

bool validateSummon(VtpSummonPlugin const * summon)
{
    if (!summon)
        return false;
    if (summon->struct_size < VTP_FIELD_END(VtpSummonPlugin, download))
        return false;
    return summon->create && summon->destroy && summon->query && summon->download;
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
        manifest = reg(kLegacyAbiVersion, &hostApi());
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
    if (manifest->struct_size < VTP_FIELD_END(VtpPluginManifest, abi_version))
    {
        loaderLog(_debug, "manifest smaller than ABI field, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }

    if (manifest->abi_version != VTP_PLUGIN_ABI_VERSION
        && manifest->abi_version != kLegacyAbiVersion)
    {
        loaderLog(_debug, "ABI version mismatch, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }

    // Defense in depth: refuse a manifest too small to contain the v2 prefix
    // fields we are about to read. ABI v3 appended summon after this prefix.
    if (manifest->struct_size < VTP_FIELD_END(VtpPluginManifest, input))
    {
        loaderLog(_debug, "manifest smaller than input field, skipping: %s", path.c_str());
        dlclose(handle);
        return;
    }

    bool supported = false;
    VtpSummonPlugin const * summon = nullptr;
    void * summonHandle = nullptr;
    std::string summonLabel;

    if (manifestHasInput(manifest))
    {
        DecoderRegistry::instance().registerInput(manifest->input);
        supported = true;
    }

    if (manifestHasSummon(manifest))
    {
        if (!validateSummon(manifest->summon))
        {
            loaderLog(_debug, "summon interface is incomplete, ignoring: %s", path.c_str());
        }
        else
        {
            summon = manifest->summon;
            summonHandle = summon->create(&hostApi());
            if (summonHandle)
            {
                summonLabel = (summon->label && *summon->label)
                                  ? summon->label
                                  : ((summon->id && *summon->id) ? summon->id : "Summon");
                supported = true;
            }
            else
            {
                loaderLog(_debug, "summon provider create failed, ignoring: %s", path.c_str());
                summon = nullptr;
            }
        }
    }

    if (!supported)
    {
        loaderLog(_debug, "manifest carries no supported interface, skipping: %s", path.c_str());
        if (summon && summon->destroy && summonHandle)
            summon->destroy(summonHandle);
        dlclose(handle);
        return;
    }

    std::string const loadedName = manifestName(manifest, path);
    loaderLog(_debug, "loaded plugin: %s", loadedName.c_str());
    _loaded.push_back(Loaded{handle, manifest, path, summon, summonHandle, summonLabel});
}

std::vector<PluginHost::PluginInfo> PluginHost::plugins() const
{
    std::vector<PluginInfo> out;
    out.reserve(_loaded.size());
    for (auto const & l : _loaded)
    {
        PluginInfo info;
        info.name = manifestName(l.manifest, l.path);
        info.version = manifestVersion(l.manifest);
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<PluginHost::SummonProvider> PluginHost::summonProviders() const
{
    std::vector<SummonProvider> out;
    for (auto const & l : _loaded)
    {
        if (!l.summon)
            continue;
        SummonProvider provider;
        provider.plugin = l.summon;
        provider.handle = l.summonHandle;
        provider.label = l.summonLabel;
        provider.pluginName = manifestName(l.manifest, l.path);
        out.push_back(std::move(provider));
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
        if (it->summon && it->summon->destroy && it->summonHandle)
            it->summon->destroy(it->summonHandle);
        if (it->handle)
            dlclose(it->handle);
    }
    _loaded.clear();
}

} // namespace vtplayer
