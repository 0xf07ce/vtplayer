// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <filesystem>

namespace vtplayer
{

/// Read file-side metadata into TrackInfo. When `filenameTitleFallback` is
/// true, an empty TITLE falls back to the file stem for library display.
TrackInfo readTrackInfo(std::filesystem::path const & path,
                        bool filenameTitleFallback = true);

} // namespace vtplayer
