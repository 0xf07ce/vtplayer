// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "ISampleSource.h"

#include <string>

struct VtpInputPlugin;

namespace vtplayer
{

/// Wraps a C-ABI input plugin (`VtpInputPlugin`) as an `ISampleSource`.
///
/// The plugin owns its decode handle; this adapter only forwards calls and
/// guards against null function pointers / failed opens. The wrapped
/// `VtpInputPlugin` pointer is owned by the plugin module and must outlive the
/// adapter — the host guarantees this by never `dlclose`ing a module while a
/// source is active.
class PluginSource : public ISampleSource
{
public:
    explicit PluginSource(VtpInputPlugin const * plugin) : _plugin(plugin) {}
    ~PluginSource() override { close(); }

    PluginSource(PluginSource const &)             = delete;
    PluginSource & operator=(PluginSource const &) = delete;

    /// Open `path` through the plugin. Returns false on a null plugin/handle.
    bool open(std::string const & path);

    /// Release the plugin decode handle. Safe to call repeatedly.
    void close();

    unsigned int read(float * out, unsigned int frames) override;
    bool         seek(double seconds) override;
    double       duration() const override { return _duration; }
    bool         seekable() const override { return _seekable; }
    bool         eof() const override { return _eof; }

    std::string const & error() const { return _error; }

private:
    VtpInputPlugin const * _plugin = nullptr;
    void *                 _handle = nullptr;
    double                 _duration = 0.0;
    bool                   _seekable = false;
    bool                   _eof      = false;
    std::string            _error;
};

} // namespace vtplayer
