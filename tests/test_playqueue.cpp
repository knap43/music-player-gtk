#include "PlayQueue.h"
#include "check.h"

#include <set>

namespace {

std::vector<Track> makeTracks(int n, const std::string &prefix = "t")
{
    std::vector<Track> tracks;
    for (int i = 0; i < n; ++i) {
        Track t;
        t.path = "/music/" + prefix + std::to_string(i) + ".flac";
        t.title = prefix + std::to_string(i);
        tracks.push_back(t);
    }
    return tracks;
}

std::string currentTitle(const PlayQueue &q)
{
    return q.currentTrack() ? q.currentTrack()->title : std::string();
}

} // namespace

TEST(linearAdvanceAndBack)
{
    PlayQueue q;
    q.append(makeTracks(3));
    CHECK_EQ(q.advance(false), 0);
    CHECK_EQ(q.advance(false), 1);
    CHECK_EQ(q.advance(false), 2);
    CHECK_EQ(q.advance(false), -1); // end of queue, repeat off
    CHECK_EQ(q.goBack(), 1);
    CHECK_EQ(q.goBack(), 0);
    CHECK_EQ(q.goBack(), -1);
}

TEST(repeatModes)
{
    PlayQueue q;
    q.append(makeTracks(2));
    q.setRepeat(PlayQueue::Repeat::All);
    q.setCurrentRow(1);
    CHECK_EQ(q.advance(false), 0); // wraps
    q.setRepeat(PlayQueue::Repeat::One);
    CHECK_EQ(q.advance(true), 0);  // finished track replays
    CHECK_EQ(q.advance(false), 1); // but "next" still moves on
}

TEST(removingCurrentContinuesWithFollower)
{
    PlayQueue q;
    q.append(makeTracks(4));
    q.setCurrentRow(1);
    q.removeRows({1});
    CHECK_EQ(q.currentRow(), -1);
    CHECK_EQ(q.advance(false), 1);
    CHECK_EQ(currentTitle(q), std::string("t2"));
}

TEST(insertAfterCurrentPlaysNext)
{
    PlayQueue q;
    q.append(makeTracks(3));
    q.setCurrentRow(0);
    const int first = q.insertAfterCurrent(makeTracks(2, "n"));
    CHECK_EQ(first, 1);
    q.advance(false);
    CHECK_EQ(currentTitle(q), std::string("n0"));
    q.advance(false);
    CHECK_EQ(currentTitle(q), std::string("n1"));
    q.advance(false);
    CHECK_EQ(currentTitle(q), std::string("t1"));
}

TEST(moveKeepsCurrent)
{
    PlayQueue q;
    q.append(makeTracks(5));
    q.setCurrentRow(2);
    CHECK_EQ(q.moveRowsTo({3, 4}, 0), 0);
    CHECK_EQ(q.trackAt(0).title, std::string("t3"));
    CHECK_EQ(q.trackAt(1).title, std::string("t4"));
    CHECK_EQ(currentTitle(q), std::string("t2"));
    CHECK_EQ(q.currentRow(), 4);
}

TEST(shufflePlaysEverythingOnce)
{
    PlayQueue q;
    q.append(makeTracks(20));
    q.setShuffle(true);
    std::set<std::string> seen;
    for (int i = 0; i < 20; ++i) {
        CHECK(q.advance(false) >= 0);
        seen.insert(currentTitle(q));
    }
    CHECK_EQ(seen.size(), 20u);
    CHECK(!q.hasNext());
    CHECK_EQ(q.advance(false), -1);
}

TEST(shuffleBackRetracesHistory)
{
    PlayQueue q;
    q.append(makeTracks(10));
    q.setShuffle(true);
    std::vector<std::string> history;
    for (int i = 0; i < 5; ++i) {
        q.advance(false);
        history.push_back(currentTitle(q));
    }
    for (int i = 3; i >= 0; --i) {
        q.goBack();
        CHECK_EQ(currentTitle(q), history[size_t(i)]);
    }
}

TEST(shuffleIncludesTracksAddedLater)
{
    PlayQueue q;
    q.append(makeTracks(5));
    q.setShuffle(true);
    q.advance(false);
    q.advance(false);
    q.append(makeTracks(5, "late"));
    std::set<std::string> seen;
    while (q.advance(false) >= 0)
        seen.insert(currentTitle(q));
    CHECK_EQ(seen.size(), 8u); // 3 unplayed originals + 5 new ones
    for (int i = 0; i < 5; ++i)
        CHECK(seen.count("late" + std::to_string(i)) == 1);
}

TEST(shuffleManualPickContinuesFromThere)
{
    PlayQueue q;
    q.append(makeTracks(6));
    q.setShuffle(true);
    q.advance(false);
    const std::string before = currentTitle(q);
    q.setCurrentRow(5);
    CHECK_EQ(currentTitle(q), std::string("t5"));
    q.goBack();
    CHECK_EQ(currentTitle(q), before);
}

TEST(shuffleRepeatAllReshuffles)
{
    PlayQueue q;
    q.append(makeTracks(4));
    q.setShuffle(true);
    q.setRepeat(PlayQueue::Repeat::All);
    for (int i = 0; i < 4; ++i)
        q.advance(false);
    const std::string last = currentTitle(q);
    CHECK(q.advance(false) >= 0);
    CHECK(currentTitle(q) != last); // never the same track twice in a row
}

TEST(signalsMirrorStructure)
{
    PlayQueue q;
    std::vector<std::string> events;
    q.signalRowsInserted.connect([&](int row, int n) { events.push_back("ins " + std::to_string(row) + " " + std::to_string(n)); });
    q.signalRowsRemoved.connect([&](int row, int n) { events.push_back("rm " + std::to_string(row) + " " + std::to_string(n)); });
    q.signalReset.connect([&] { events.push_back("reset"); });
    q.append(makeTracks(3));
    q.removeRows({0, 2});
    q.clear();
    CHECK_EQ(events.size(), 4u);
    CHECK_EQ(events[0], std::string("ins 0 3"));
    CHECK_EQ(events[1], std::string("rm 2 1"));
    CHECK_EQ(events[2], std::string("rm 0 1"));
    CHECK_EQ(events[3], std::string("reset"));
}

TEST(moveMirrorsAsRemoveAndInsert)
{
    PlayQueue q;
    q.append(makeTracks(5));
    std::vector<std::string> events;
    q.signalRowsInserted.connect([&](int row, int n) { events.push_back("ins " + std::to_string(row) + " " + std::to_string(n)); });
    q.signalRowsRemoved.connect([&](int row, int n) { events.push_back("rm " + std::to_string(row) + " " + std::to_string(n)); });
    q.signalReset.connect([&] { events.push_back("reset"); });
    CHECK_EQ(q.moveRowsTo({1, 3}, 5), 3);
    CHECK_EQ(q.trackAt(3).title, std::string("t1"));
    CHECK_EQ(q.trackAt(4).title, std::string("t3"));
    CHECK_EQ(events.size(), 3u);
    CHECK_EQ(events[0], std::string("rm 3 1"));
    CHECK_EQ(events[1], std::string("rm 1 1"));
    CHECK_EQ(events[2], std::string("ins 3 2"));
    // Dropping a block onto its own place changes nothing.
    events.clear();
    CHECK_EQ(q.moveRowsTo({3, 4}, 3), 3);
    CHECK_EQ(q.moveRowsTo({3, 4}, 5), 3);
    CHECK(events.empty());
}

int main()
{
    return check::run();
}
