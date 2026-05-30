// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "DecoderRegistry.h"

#include "vtplayer/plugin.h"

#include <cctype>

namespace vtplayer
{

DecoderRegistry & DecoderRegistry::instance()
{
    static DecoderRegistry reg;
    return reg;
}

std::string DecoderRegistry::normalize(std::string_view ext)
{
    std::string out;
    out.reserve(ext.size());
    std::size_t i = 0;
    if (!ext.empty() && ext.front() == '.')
        i = 1; // tolerate a leading dot defensively
    for (; i < ext.size(); ++i)
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ext[i]))));
    return out;
}

void DecoderRegistry::registerInput(VtpInputPlugin const * plugin)
{
    if (!plugin || !plugin->exts)
        return;

    for (std::size_t i = 0; i < plugin->n_exts; ++i)
    {
        char const * raw = plugin->exts[i];
        if (!raw || !*raw)
            continue;
        std::string ext = normalize(raw);
        if (ext.empty())
            continue;
        // First claim wins; do not let a later plugin shadow an extension.
        _byExt.emplace(std::move(ext), plugin);
    }
}

VtpInputPlugin const * DecoderRegistry::find(std::string_view ext) const
{
    auto it = _byExt.find(normalize(ext));
    return it == _byExt.end() ? nullptr : it->second;
}

std::vector<std::string> DecoderRegistry::extensions() const
{
    std::vector<std::string> out;
    out.reserve(_byExt.size());
    for (auto const & kv : _byExt)
        out.push_back(kv.first);
    return out;
}

} // namespace vtplayer
