// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct VtpInputPlugin;

namespace vtplayer
{

/// Process-wide map from file extension to the input plugin that owns it.
///
/// Populated once at startup by `PluginHost` (single-threaded, before audio
/// and the library scanner run) and read-only afterwards from the UI and
/// scanner threads. The stored `VtpInputPlugin*` pointers are owned by the
/// plugin's static data and stay valid until the host `dlclose`s the module,
/// so `clear()` must run before any unload.
class DecoderRegistry
{
public:
    static DecoderRegistry & instance();

    /// Register every extension advertised by `plugin`. The first plugin to
    /// claim an extension wins; later claims for the same extension are
    /// ignored. Extensions are normalized to lowercase, no leading dot.
    void registerInput(VtpInputPlugin const * plugin);

    /// The plugin owning `ext` (lowercase, no dot), or nullptr if none.
    VtpInputPlugin const * find(std::string_view ext) const;

    /// All registered extensions (lowercase, no dot), unordered.
    std::vector<std::string> extensions() const;

    bool empty() const { return _byExt.empty(); }

    /// Drop all registrations. Call before the owning modules are unloaded.
    void clear() { _byExt.clear(); }

private:
    DecoderRegistry() = default;

    static std::string normalize(std::string_view ext);

    std::unordered_map<std::string, VtpInputPlugin const *> _byExt;
};

} // namespace vtplayer
