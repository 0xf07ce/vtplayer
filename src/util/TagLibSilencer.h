// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace vtplayer
{

/// Install a no-op TagLib debug listener so library scanning never spills
/// "Invalid UTF16 string" / "BOM is broken" warnings onto the terminal UI.
///
/// TagLib writes those warnings straight to stderr for every file whose
/// ID3v2 text frames use UTF-16 without a valid BOM (common in legacy
/// Korean tags). Call once at startup, before any TagLib::FileRef is opened.
void silenceTagLib();

} // namespace vtplayer
