// Copyright (c) 2026 Leon J. Lee
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_framework.h"

#include "playqueue/PlayQueue.h"

using namespace vtplayer;

namespace
{
TrackInfo track(std::string title)
{
    TrackInfo t;
    t.title = std::move(title);
    return t;
}
} // namespace

TEST_CASE("PlayQueue starts empty")
{
    PlayQueue q;
    CHECK(q.empty());
    CHECK_EQ(q.size(), 0);
    CHECK(q.at(0) == nullptr);
}

TEST_CASE("PlayQueue addTrack appends in order")
{
    PlayQueue q;
    q.addTrack(track("a"));
    q.addTrack(track("b"));
    CHECK_EQ(q.size(), 2);
    CHECK_EQ(q.at(0)->title, std::string("a"));
    CHECK_EQ(q.at(1)->title, std::string("b"));
}

TEST_CASE("PlayQueue insertTrack clamps the index to [0, size]")
{
    PlayQueue q;
    q.addTrack(track("a"));
    q.addTrack(track("b"));
    q.insertTrack(-5, track("front")); // clamps to 0
    q.insertTrack(999, track("back"));  // clamps to size
    CHECK_EQ(q.size(), 4);
    CHECK_EQ(q.at(0)->title, std::string("front"));
    CHECK_EQ(q.at(3)->title, std::string("back"));
}

TEST_CASE("PlayQueue removeAt ignores out-of-range indices")
{
    PlayQueue q;
    q.addTrack(track("a"));
    q.removeAt(-1);
    q.removeAt(10);
    CHECK_EQ(q.size(), 1);
    q.removeAt(0);
    CHECK(q.empty());
}

TEST_CASE("PlayQueue swap exchanges entries and no-ops on bad indices")
{
    PlayQueue q;
    q.addTrack(track("a"));
    q.addTrack(track("b"));
    q.swap(0, 1);
    CHECK_EQ(q.at(0)->title, std::string("b"));
    CHECK_EQ(q.at(1)->title, std::string("a"));
    q.swap(0, 0);   // equal -> no-op
    q.swap(0, 5);   // out of range -> no-op
    CHECK_EQ(q.at(0)->title, std::string("b"));
}

TEST_CASE("PlayQueue clear empties the queue")
{
    PlayQueue q;
    q.addTrack(track("a"));
    q.addTrack(track("b"));
    q.clear();
    CHECK(q.empty());
}
