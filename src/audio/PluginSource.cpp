// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PluginSource.h"

#include "vtplayer/plugin.h"

namespace vtplayer
{

bool PluginSource::open(std::string const & path)
{
    if (!_plugin || !_plugin->open)
    {
        _error = "plugin has no open()";
        return false;
    }

    _handle = _plugin->open(path.c_str());
    if (!_handle)
    {
        _error = "plugin open failed";
        return false;
    }

    _duration = _plugin->duration ? _plugin->duration(_handle) : 0.0;
    _seekable = _plugin->seekable ? (_plugin->seekable(_handle) != 0) : false;
    _eof      = false;
    _error.clear();
    return true;
}

void PluginSource::close()
{
    if (_handle && _plugin && _plugin->close)
        _plugin->close(_handle);
    _handle = nullptr;
}

unsigned int PluginSource::read(float * out, unsigned int frames)
{
    if (!_handle || !_plugin || !_plugin->read || frames == 0)
        return 0;

    unsigned int const got = _plugin->read(_handle, out, frames);
    // A short read from a (non-blocking, synthesis-style) input plugin means
    // the source is spent — mirror Decoder's EOF semantics so AudioEngine
    // flags track-end exactly the same way.
    if (got < frames)
        _eof = true;
    return got;
}

bool PluginSource::seek(double seconds)
{
    if (!_handle || !_plugin || !_plugin->seek)
        return false;
    bool const ok = _plugin->seek(_handle, seconds) == 0;
    if (ok)
        _eof = false;
    return ok;
}

} // namespace vtplayer
