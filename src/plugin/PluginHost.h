// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct VtpPluginManifest;

namespace vtplayer
{

/// Loads `.so` / `.dylib` plugins and keeps their modules alive.
///
/// A plugin is loaded by `dlopen`, resolving the single entry symbol
/// `vtp_register`, calling it with the host ABI version and the host service
/// table, then validating the returned manifest's ABI version and kind. Input
/// plugins register their extensions into `DecoderRegistry`. Any failure logs
/// and skips — a bad plugin never crashes the host.
///
/// Lifetime: modules stay loaded until `shutdown()`. Because an active audio
/// source may hold pointers into a plugin's code pages, `shutdown()` MUST run
/// only after the AudioEngine has stopped (see `Application::cleanup`). The
/// destructor also calls `shutdown()` as a backstop.
class PluginHost
{
public:
    PluginHost() = default;
    ~PluginHost();

    PluginHost(PluginHost const &)             = delete;
    PluginHost & operator=(PluginHost const &) = delete;

    /// Raise log verbosity: plugin and loader diagnostics go to stderr.
    /// Off by default so the TUI is not corrupted. Call before loadAll().
    void setDebug(bool debug);

    /// Load every plugin in the default directory (`~/.config/vtplayer/plugins`).
    /// Missing directory is fine (no plugins).
    void loadAll();

    /// Load every loadable file directly under `dir`. Exposed for tests.
    void loadFrom(std::filesystem::path const & dir);

    /// Clear the decoder registry and `dlclose` every module, newest first.
    /// Idempotent. Do not call while an audio source is active.
    void shutdown();

    /// Number of currently loaded plugins.
    std::size_t count() const { return _loaded.size(); }

    /// Lightweight, copyable view of a loaded plugin for UI listing.
    struct PluginInfo
    {
        std::string name;    ///< manifest name, or the file name as a fallback
        std::string version; ///< manifest version, or "0.0.0" when absent
    };

    /// Snapshot of every currently loaded plugin, in load order.
    std::vector<PluginInfo> plugins() const;

    /// Canonical plugin directory: `~/.config/vtplayer/plugins`. Empty if
    /// $HOME is unset.
    static std::filesystem::path defaultDir();

private:
    struct Loaded
    {
        void *                    handle   = nullptr;
        VtpPluginManifest const * manifest = nullptr;
        std::string               path;
    };

    void loadOne(std::filesystem::path const & file);

    std::vector<Loaded> _loaded;
    bool                _debug = false;
};

} // namespace vtplayer
