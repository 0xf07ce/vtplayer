// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "TagLibSilencer.h"

// TagLib v2 (consumed via FetchContent / add_subdirectory): bare header
// names — the deps target adds `taglib/` and `taglib/toolkit/` to the
// include path.
#include <tdebuglistener.h>
#include <tstring.h>

namespace vtplayer
{

namespace
{

/// Swallows every TagLib debug message instead of writing it to stderr.
class SilentDebugListener : public TagLib::DebugListener
{
public:
    void printMessage(TagLib::String const &) override {}
};

} // namespace

void silenceTagLib()
{
    // Leaked intentionally: the listener must outlive every TagLib call
    // for the whole process lifetime, and TagLib does not take ownership.
    static SilentDebugListener listener;
    TagLib::setDebugListener(&listener);
}

} // namespace vtplayer
