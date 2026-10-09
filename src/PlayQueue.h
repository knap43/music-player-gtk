#pragma once

#include "Track.h"

#include <sigc++/sigc++.h>

#include <cstdint>
#include <random>
#include <vector>

// The play queue: an ordered list of tracks plus the notion of a "current"
// entry. Played tracks stay in the queue so that "previous" works, and so that
// the queue can be replayed with repeat enabled.
//
// With shuffle enabled the queue keeps a separate random play order. Tracks
// added while shuffling are slotted in at random positions among the tracks
// that have not been played yet, and a manually picked track becomes the new
// point from which shuffled playback continues.
//
// Every entry has a stable id, so views can refer to entries across edits.
class PlayQueue
{
public:
    enum class Repeat { Off, All, One };

    PlayQueue();

    int count() const { return int(m_entries.size()); }
    bool isEmpty() const { return m_entries.empty(); }
    const Track &trackAt(int row) const { return m_entries[size_t(row)].track; }
    uint64_t idAt(int row) const { return m_entries[size_t(row)].id; }
    int rowOfId(uint64_t id) const;
    std::vector<Track> tracks() const;
    int64_t totalDurationMs() const;

    // Editing. `row` is clamped; returns the row of the first inserted track.
    int insert(int row, const std::vector<Track> &tracks);
    int append(const std::vector<Track> &tracks) { return insert(count(), tracks); }
    int insertAfterCurrent(const std::vector<Track> &tracks);
    void removeRows(std::vector<int> rows);
    // Returns the new row of the first moved entry, or -1 if nothing moved.
    int moveRowsTo(std::vector<int> rows, int destination);
    void clear();
    void updateTrack(int row, const Track &track);

    // Playback position.
    int currentRow() const;
    uint64_t currentId() const { return m_currentId; }
    const Track *currentTrack() const;
    void setCurrentRow(int row);
    // Advances and returns the new current row, or -1 at the end of the queue.
    // `automatic` is true when the previous track simply finished, which is the
    // only case where Repeat::One replays the same track.
    int advance(bool automatic);
    int goBack();
    // Whether advance(false) / goBack() would find a track.
    bool hasNext() const;
    bool hasPrevious() const;

    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    Repeat repeat() const { return m_repeat; }
    void setRepeat(Repeat mode);

    // Structural changes, for views mirroring the queue.
    sigc::signal<void(int, int)> signalRowsInserted; // first row, count
    sigc::signal<void(int, int)> signalRowsRemoved;  // first row, count
    sigc::signal<void()> signalReset;                // everything changed
    sigc::signal<void(int)> signalRowUpdated;
    // currentRowChanged(new row, previous id)
    sigc::signal<void(int, uint64_t)> signalCurrentChanged;
    sigc::signal<void(bool)> signalShuffleChanged;
    sigc::signal<void(Repeat)> signalRepeatChanged;
    sigc::signal<void()> signalContentsChanged;

private:
    struct Entry {
        uint64_t id = 0;
        Track track;
    };

    void setCurrentId(uint64_t id);
    void rebuildShuffleOrder();
    int orderIndexOf(uint64_t id) const;

    std::vector<Entry> m_entries;
    uint64_t m_nextId = 1;
    uint64_t m_currentId = 0;
    // Linear mode: where playback resumes when the current entry was removed.
    int m_resumeRow = 0;

    bool m_shuffle = false;
    std::vector<uint64_t> m_order; // shuffled ids
    int m_orderPos = -1;           // position of the current entry in m_order
    Repeat m_repeat = Repeat::Off;
    std::mt19937 m_rng;
};
