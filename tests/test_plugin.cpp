// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

// End-to-end test of the plugin host: load real .so/.dylib modules built from
// tests/plugins, verify the ABI gate, drive a PluginSource through the
// registered input plugin, and confirm unload clears the registry.
//
// DUMMY_PLUGIN_DIR is injected by CMake and points at the directory holding
// the freshly-built dummy plugin modules.

#include "test_framework.h"

#include "audio/PluginSource.h"
#include "plugin/DecoderRegistry.h"
#include "plugin/PluginHost.h"
#include "vtplayer/plugin.h"

#include <cmath>
#include <vector>

#ifndef DUMMY_PLUGIN_DIR
#define DUMMY_PLUGIN_DIR "."
#endif

using namespace vtplayer;

// Single case so the shared DecoderRegistry singleton is driven in a
// deterministic order (load → use → unload) within one process.
TEST_CASE("plugin host load / decode / abi-gate / unload")
{
    auto & reg = DecoderRegistry::instance();
    reg.clear();

    PluginHost host;
    host.setDebug(false);
    host.loadFrom(DUMMY_PLUGIN_DIR);

    // The good plugin registered "sine"; the bad-ABI plugin must be skipped,
    // so its "bad" extension is absent.
    VtpInputPlugin const * sine = reg.find("sine");
    CHECK(sine != nullptr);
    CHECK(reg.find("SINE") != nullptr);   // case-insensitive lookup
    CHECK(reg.find(".sine") != nullptr);  // tolerant of a leading dot
    CHECK(reg.find("bad") == nullptr);    // ABI 999 → skipped
    CHECK(host.count() >= 1);

    if (sine)
    {
        // Drive the plugin through the same adapter AudioEngine uses.
        PluginSource src(sine);
        CHECK(src.open("ignored.sine"));
        CHECK(src.seekable());
        CHECK(std::fabs(src.duration() - 1.0) < 1e-6); // 1s of audio
        CHECK(!src.eof());

        // Pull a block; the sine is non-silent so at least one sample is != 0.
        std::vector<float> buf(2 * 1024, 0.0f);
        unsigned got = src.read(buf.data(), 1024);
        CHECK(got == 1024);
        bool nonZero = false;
        for (float v : buf)
            if (v != 0.0f) { nonZero = true; break; }
        CHECK(nonZero);

        // Drain to the end: total is exactly 44100 frames. Reading well past
        // that returns a short read and flags eof().
        unsigned total = got;
        for (int i = 0; i < 100 && !src.eof(); ++i)
            total += src.read(buf.data(), 1024);
        CHECK(src.eof());
        CHECK(total == 44100u);

        // Seeking back clears eof and lets us read again.
        CHECK(src.seek(0.0));
        CHECK(!src.eof());
        CHECK(src.read(buf.data(), 1024) == 1024);
    }

    // read_tags surfaces plugin-provided metadata via the C ABI.
    if (sine && sine->read_tags)
    {
        VtpTagOut tags{};
        tags.struct_size = sizeof(tags);
        CHECK(sine->read_tags("ignored.sine", &tags) == 0);
        CHECK(std::string(tags.title) == "Sine Test Tone");
        CHECK(std::string(tags.artist) == "vtplayer");
        CHECK(tags.year == 2026);
    }

    // Unload drops registry references before dlclose.
    host.shutdown();
    CHECK(reg.empty());
    CHECK(reg.find("sine") == nullptr);
}
