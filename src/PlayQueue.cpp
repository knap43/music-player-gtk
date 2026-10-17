#include "PlayQueue.h"

#include <algorithm>
#include <functional>

PlayQueue::PlayQueue()
    : m_rng(std::random_device{}())
{
}

std::vector<Track> PlayQueue::tracks() const
{
    std::vector<Track> result;
    result.reserve(m_entries.size());
    for (const Entry &e : m_entries)
        result.push_back(e.track);
    return result;
}

int64_t PlayQueue::totalDurationMs() const
{
    int64_t total = 0;
    for (const Entry &e : m_entries)
        total += e.track.durationMs;
    return total;
}

int PlayQueue::rowOfId(uint64_t id) const
{
    if (id == 0)
        return -1;
    for (int i = 0; i < count(); ++i)
        if (m_entries[size_t(i)].id == id)
            return i;
    return -1;
}

int PlayQueue::orderIndexOf(uint64_t id) const
{
    const auto it = std::find(m_order.begin(), m_order.end(), id);
    return it == m_order.end() ? -1 : int(it - m_order.begin());
}

int PlayQueue::currentRow() const
{
    return rowOfId(m_currentId);
}

const Track *PlayQueue::currentTrack() const
{
    const int row = currentRow();
    return row >= 0 ? &m_entries[size_t(row)].track : nullptr;
}

void PlayQueue::setCurrentId(uint64_t id)
{
    const uint64_t old = m_currentId;
    m_currentId = id;
    signalCurrentChanged.emit(currentRow(), old);
}

int PlayQueue::insert(int row, const std::vector<Track> &tracks)
{
    row = std::clamp(row, 0, count());
    if (tracks.empty())
        return row;

    std::vector<Entry> entries;
    entries.reserve(tracks.size());
    for (const Track &t : tracks)
        entries.push_back({m_nextId++, t});
    m_entries.insert(m_entries.begin() + row, entries.begin(), entries.end());

    if (m_currentId == 0 && row < m_resumeRow)
        m_resumeRow += int(tracks.size());

    if (m_shuffle) {
        // Scatter the new tracks among the ones that have not played yet.
        for (const Entry &e : entries) {
            std::uniform_int_distribution<int> dist(m_orderPos + 1, int(m_order.size()));
            m_order.insert(m_order.begin() + dist(m_rng), e.id);
        }
    }

    signalRowsInserted.emit(row, int(tracks.size()));
    signalContentsChanged.emit();
    return row;
}

int PlayQueue::insertAfterCurrent(const std::vector<Track> &tracks)
{
    int row = currentRow();
    row = row >= 0 ? row + 1 : m_resumeRow;
    const int first = insert(row, tracks);
    if (m_shuffle) {
        // "Play next" must mean next, even when shuffling.
        std::vector<uint64_t> ids;
        for (size_t i = 0; i < tracks.size(); ++i)
            ids.push_back(m_entries[size_t(first) + i].id);
        for (uint64_t id : ids) {
            const int at = orderIndexOf(id);
            if (at >= 0)
                m_order.erase(m_order.begin() + at);
        }
        for (size_t i = 0; i < ids.size(); ++i)
            m_order.insert(m_order.begin() + (m_orderPos + 1 + int(i)), ids[i]);
    }
    return first;
}

void PlayQueue::removeRows(std::vector<int> rows)
{
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.empty())
        return;

    const int current = currentRow();
    const uint64_t oldCurrent = m_currentId;
    for (int row : rows) {
        if (row < 0 || row >= count())
            continue;
        const uint64_t id = m_entries[size_t(row)].id;

        if (id == m_currentId) {
            m_currentId = 0;
            m_resumeRow = row;
        } else if (m_currentId == 0 && row < m_resumeRow) {
            --m_resumeRow;
        }

        const int orderIndex = orderIndexOf(id);
        if (orderIndex >= 0) {
            m_order.erase(m_order.begin() + orderIndex);
            if (orderIndex <= m_orderPos)
                --m_orderPos;
        }

        m_entries.erase(m_entries.begin() + row);
        signalRowsRemoved.emit(row, 1);
    }

    if ((current >= 0 && m_currentId == 0) || current != currentRow())
        signalCurrentChanged.emit(currentRow(), oldCurrent);
    signalContentsChanged.emit();
}

int PlayQueue::moveRowsTo(std::vector<int> rows, int destination)
{
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    rows.erase(std::remove_if(rows.begin(), rows.end(), [this](int r) { return r < 0 || r >= count(); }),
               rows.end());
    if (rows.empty())
        return -1;

    destination = std::clamp(destination, 0, count());
    int before = 0;
    for (int r : rows)
        if (r < destination)
            ++before;

    const int target = destination - before;
    // A contiguous block dropped onto its own place: nothing to do.
    if (rows.back() - rows.front() + 1 == int(rows.size()) && rows.front() == target)
        return target;

    // Linear mode with no current entry: keep resuming from the same entry.
    const uint64_t resumeId = (m_currentId == 0 && m_resumeRow < count()) ? idAt(m_resumeRow) : 0;

    // Mirror the move as removals and one insertion rather than a reset, so
    // views keep their scroll position and the rows that did not move.
    std::vector<Entry> moved;
    for (int i = int(rows.size()) - 1; i >= 0; --i) {
        const int row = rows[size_t(i)];
        moved.insert(moved.begin(), m_entries[size_t(row)]);
        m_entries.erase(m_entries.begin() + row);
        signalRowsRemoved.emit(row, 1);
    }
    m_entries.insert(m_entries.begin() + target, moved.begin(), moved.end());
    signalRowsInserted.emit(target, int(moved.size()));

    if (resumeId != 0)
        m_resumeRow = rowOfId(resumeId);
    signalContentsChanged.emit();
    return target;
}

void PlayQueue::clear()
{
    const uint64_t old = m_currentId;
    m_entries.clear();
    m_order.clear();
    m_orderPos = -1;
    m_currentId = 0;
    m_resumeRow = 0;
    signalReset.emit();
    signalCurrentChanged.emit(-1, old);
    signalContentsChanged.emit();
}

void PlayQueue::updateTrack(int row, const Track &track)
{
    if (row < 0 || row >= count())
        return;
    m_entries[size_t(row)].track = track;
    signalRowUpdated.emit(row);
    signalContentsChanged.emit();
}

void PlayQueue::setCurrentRow(int row)
{
    if (row < 0 || row >= count()) {
        setCurrentId(0);
        return;
    }
    const uint64_t id = m_entries[size_t(row)].id;
    if (m_shuffle) {
        // Continue shuffled playback from the chosen track.
        const int at = orderIndexOf(id);
        if (at >= 0) {
            m_order.erase(m_order.begin() + at);
            if (at <= m_orderPos)
                --m_orderPos;
        }
        m_order.insert(m_order.begin() + (m_orderPos + 1), id);
        ++m_orderPos;
    }
    setCurrentId(id);
}

int PlayQueue::advance(bool automatic)
{
    if (isEmpty())
        return -1;

    if (automatic && m_repeat == Repeat::One && m_currentId != 0) {
        signalCurrentChanged.emit(currentRow(), m_currentId);
        return currentRow();
    }

    if (m_shuffle) {
        int pos = m_orderPos + 1;
        if (pos >= int(m_order.size())) {
            if (m_repeat == Repeat::Off)
                return -1;
            const uint64_t last = m_currentId;
            rebuildShuffleOrder();
            // Avoid playing the same track twice in a row across a reshuffle.
            if (m_order.size() > 1 && m_order.front() == last)
                std::swap(m_order[0], m_order[1 + size_t(m_rng() % (m_order.size() - 1))]);
            pos = 0;
        }
        m_orderPos = pos;
        setCurrentId(m_order[size_t(pos)]);
        return currentRow();
    }

    const int current = currentRow();
    int next = current >= 0 ? current + 1 : m_resumeRow;
    if (next >= count()) {
        if (m_repeat == Repeat::Off)
            return -1;
        next = 0;
    }
    setCurrentId(m_entries[size_t(next)].id);
    return next;
}

int PlayQueue::goBack()
{
    if (isEmpty())
        return -1;

    if (m_shuffle) {
        int pos = m_orderPos - 1;
        if (pos < 0) {
            if (m_repeat == Repeat::Off || m_order.empty())
                return -1;
            pos = int(m_order.size()) - 1;
        }
        m_orderPos = pos;
        setCurrentId(m_order[size_t(pos)]);
        return currentRow();
    }

    const int current = currentRow();
    int previous = (current >= 0 ? current : m_resumeRow) - 1;
    if (previous < 0) {
        if (m_repeat == Repeat::Off)
            return -1;
        previous = count() - 1;
    }
    setCurrentId(m_entries[size_t(previous)].id);
    return previous;
}

bool PlayQueue::hasNext() const
{
    if (isEmpty())
        return false;
    if (m_repeat != Repeat::Off)
        return true;
    if (m_shuffle)
        return m_orderPos + 1 < int(m_order.size());
    const int current = currentRow();
    return (current >= 0 ? current + 1 : m_resumeRow) < count();
}

bool PlayQueue::hasPrevious() const
{
    if (isEmpty())
        return false;
    if (m_repeat != Repeat::Off)
        return true;
    if (m_shuffle)
        return m_orderPos > 0;
    const int current = currentRow();
    return (current >= 0 ? current : m_resumeRow) > 0;
}

void PlayQueue::rebuildShuffleOrder()
{
    m_order.clear();
    for (const Entry &e : m_entries)
        if (e.id != m_currentId)
            m_order.push_back(e.id);
    std::shuffle(m_order.begin(), m_order.end(), m_rng);
    m_orderPos = -1;
}

void PlayQueue::setShuffle(bool on)
{
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    if (on) {
        rebuildShuffleOrder();
        if (m_currentId != 0) {
            m_order.insert(m_order.begin(), m_currentId);
            m_orderPos = 0;
        }
    } else {
        m_order.clear();
        m_orderPos = -1;
    }
    signalShuffleChanged.emit(on);
}

void PlayQueue::setRepeat(Repeat mode)
{
    if (mode == m_repeat)
        return;
    m_repeat = mode;
    signalRepeatChanged.emit(mode);
}
