// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PlayQueueRepository.h"

#include <cstdlib>
#include <system_error>

namespace vtplayer
{

std::filesystem::path PlayQueueRepository::path()
{
    char const * home = std::getenv("HOME");
    if (!home) return {};
    return std::filesystem::path(home) / ".config" / "ventty-player" / "playqueue.m3u";
}

bool PlayQueueRepository::ensureDirectory()
{
    auto file = path();
    if (file.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return !ec;
}

PlayQueue PlayQueueRepository::load()
{
    auto file = path();
    ensureDirectory();

    if (auto loaded = PlayQueue::load(file))
    {
        return std::move(*loaded);
    }

    PlayQueue empty(file);
    empty.save();
    return empty;
}

} // namespace vtplayer
