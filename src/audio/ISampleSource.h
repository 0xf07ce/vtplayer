// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace vtplayer
{

/// Common interface for everything `AudioEngine` can play.
///
/// Every implementation produces the same normalized stream — interleaved
/// float32, stereo, 44.1 kHz — so the engine, gain stage and visualizer never
/// branch on the source kind. `Decoder` (libav files), `StreamSource` (network
/// radio) and `PluginSource` (input plugins) all implement it.
///
/// Threading: `read()` runs on the audio thread; `seek()` runs on the UI
/// thread. `AudioEngine` serializes them with its `_audioMutex`. File-backed
/// `read()` must not block on I/O (see the plugin ABI realtime contract).
struct ISampleSource
{
    virtual ~ISampleSource() = default;

    /// Pull up to `frames` stereo frames into `out` (frames*2 floats).
    /// Returns the number of frames written; a short return means the source
    /// is short of data (EOF for files, underrun for streams). The caller
    /// zero-fills the remainder.
    virtual unsigned int read(float * out, unsigned int frames) = 0;

    /// Seek to `seconds`. Returns false when unsupported (live streams) or on
    /// error. A true return implies the read cursor moved.
    virtual bool seek(double seconds) = 0;

    /// Total duration in seconds; <= 0 when unknown (live streams).
    virtual double duration() const = 0;

    /// True when seek() can succeed for this source.
    virtual bool seekable() const = 0;

    /// True once the source has reached its end and drained. For files this is
    /// EOF; for streams it means the upstream closed AND the buffer is empty.
    /// A transient stream underrun is NOT eof().
    virtual bool eof() const = 0;

    /// True for live network streams (drives the transport "LIVE" indicator
    /// and the runtime gain path). False for files and plugin sources.
    virtual bool isStream() const { return false; }

    /// True while a stream is (re)buffering and emitting silence. Always false
    /// for non-stream sources.
    virtual bool buffering() const { return false; }
};

} // namespace vtplayer
