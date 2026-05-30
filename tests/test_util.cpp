// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include "util/M3uReader.h"
#include "util/PlsReader.h"
#include "util/UnicodeNormalize.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

// Write `content` to a uniquely-named temp file with the given extension and
// return its path. Caller is responsible for nothing — temp files are small
// and cleaned up by the OS; tests do not assert on absolute paths.
std::filesystem::path writeTemp(std::string const & stem, std::string const & ext,
                                std::string const & content)
{
    static int counter = 0;
    auto dir  = std::filesystem::temp_directory_path() / "vtplayer_tests";
    std::filesystem::create_directories(dir);
    auto path = dir / (stem + "_" + std::to_string(counter++) + ext);
    std::ofstream(path, std::ios::trunc) << content;
    return path;
}

} // namespace

using namespace vtplayer;

// ---------------------------------------------------------------------------
// UnicodeNormalize
// ---------------------------------------------------------------------------

TEST_CASE("toNfc passes ASCII through unchanged")
{
    CHECK_EQ(toNfc("Hello, world!"), std::string("Hello, world!"));
    CHECK_EQ(toNfc(""), std::string());
}

TEST_CASE("toNfc composes decomposed Hangul (NFD -> NFC)")
{
    // U+1100 (ᄀ) + U+1161 (ᅡ)  =>  U+AC00 (가)
    std::string const nfd = "\xE1\x84\x80\xE1\x85\xA1";
    std::string const nfc = "\xEA\xB0\x80";
    CHECK_EQ(toNfc(nfd), nfc);
    // Already-composed input is idempotent.
    CHECK_EQ(toNfc(nfc), nfc);
}

TEST_CASE("truncateToWidth leaves short strings untouched")
{
    CHECK_EQ(truncateToWidth("hello", 10), std::string("hello"));
}

TEST_CASE("truncateToWidth shortens and never exceeds the width budget")
{
    auto const out = truncateToWidth("hello world this is long", 8);
    // Result must fit in 8 display cells (ASCII = 1 cell each).
    CHECK(out.size() <= 8u);
    CHECK(out != std::string("hello world this is long"));
}

// ---------------------------------------------------------------------------
// M3uReader
// ---------------------------------------------------------------------------

TEST_CASE("M3uReader returns nullopt for a missing file")
{
    auto r = M3uReader::read("/no/such/playlist.m3u");
    CHECK(!r.has_value());
}

TEST_CASE("M3uReader parses #EXTINF artist/title/duration and skips other tags")
{
    auto path = writeTemp("list", ".m3u",
                          "#EXTM3U\n"
                          "#PLAYLIST:ignored\n"
                          "#EXTINF:123,Artist Name - Song Title\n"
                          "song.mp3\n");
    auto r = M3uReader::read(path);
    CHECK(r.has_value());
    CHECK_EQ(r->size(), 1u);
    CHECK_EQ((*r)[0].artist, std::string("Artist Name"));
    CHECK_EQ((*r)[0].title, std::string("Song Title"));
    CHECK_EQ((*r)[0].duration, 123.0f);
    CHECK((*r)[0].format == AudioFormat::Mp3);
}

TEST_CASE("M3uReader falls back to filename stem when no #EXTINF precedes a path")
{
    auto path = writeTemp("list", ".m3u", "track.flac\n");
    auto r = M3uReader::read(path);
    CHECK(r.has_value());
    CHECK_EQ(r->size(), 1u);
    CHECK_EQ((*r)[0].title, std::string("track"));
    CHECK((*r)[0].format == AudioFormat::Flac);
}

// ---------------------------------------------------------------------------
// PlsReader
// ---------------------------------------------------------------------------

TEST_CASE("PlsReader returns nullopt for a missing file")
{
    auto r = PlsReader::read("/no/such/stations.pls");
    CHECK(!r.has_value());
}

TEST_CASE("PlsReader synthesizes stream channels with album = pls stem")
{
    auto path = writeTemp("myradio", ".pls",
                          "[playlist]\n"
                          "File1=http://example.com/stream\n"
                          "Title1=Cool Radio\n"
                          "Length1=-1\n"
                          "File2=http://other.net:8000/live\n"
                          "NumberOfEntries=2\n"
                          "Version=2\n");
    auto r = PlsReader::read(path);
    CHECK(r.has_value());
    CHECK_EQ(r->size(), 2u);

    auto const & ch1 = (*r)[0];
    CHECK(ch1.isStream());
    CHECK_EQ(ch1.streamUrl, std::string("http://example.com/stream"));
    CHECK_EQ(ch1.title, std::string("Cool Radio"));
    CHECK_EQ(ch1.duration, 0.0f);             // Length=-1 -> unknown
    CHECK(ch1.format == AudioFormat::Stream);
    CHECK(ch1.album.rfind("myradio", 0) == 0); // album == pls stem
    CHECK(ch1.path.string().find("#CH1") != std::string::npos);

    auto const & ch2 = (*r)[1];
    CHECK(ch2.isStream());
    // No Title2 -> falls back to the URL host.
    CHECK_EQ(ch2.title, std::string("other.net:8000"));
}

TEST_CASE("PlsReader skips entries lacking a File value")
{
    auto path = writeTemp("partial", ".pls",
                          "[playlist]\n"
                          "Title1=Orphan title with no file\n"
                          "File2=http://example.com/ok\n");
    auto r = PlsReader::read(path);
    CHECK(r.has_value());
    CHECK_EQ(r->size(), 1u);
    CHECK_EQ((*r)[0].streamUrl, std::string("http://example.com/ok"));
}
