// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "../audio/TrackInfo.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace vtplayer
{

/// Parser for PLS (`.pls`) playlist files. PLS is an INI variant with a
/// `[playlist]` section containing `FileN=`, `TitleN=`, `LengthN=` triples for
/// each entry. URL entries become library-indexable stream rows whose
/// `TrackInfo::path` is synthesized as `<pls_absolute_path>#CH<N>` and whose
/// `streamUrl` carries the URL; local-file entries resolve like M3U entries.
namespace PlsReader
{

/// Returns std::nullopt if the file cannot be opened. Returns an empty vector
/// for a parseable-but-empty playlist. Entries lacking a usable `FileN` value
/// are skipped. `TrackInfo::album` is set to the `.pls` file's stem so all
/// channels group naturally in LibraryView.
std::optional<std::vector<TrackInfo>> read(std::filesystem::path const & path);

} // namespace PlsReader

} // namespace vtplayer
