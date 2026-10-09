#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LyricLine {
    int64_t timeMs = -1; // -1 for unsynchronised lyrics
    std::string text;
};

// Plain or LRC-style time-synchronised lyrics.
class Lyrics
{
public:
    static Lyrics parse(const std::string &text);
    static Lyrics fromLines(std::vector<LyricLine> lines, bool synced);

    bool isEmpty() const { return m_lines.empty(); }
    bool isSynced() const { return m_synced; }
    const std::vector<LyricLine> &lines() const { return m_lines; }

    // Index of the line being sung at the given position, or -1 before the
    // first timestamp (and always -1 for unsynchronised lyrics).
    int lineAt(int64_t positionMs) const;

private:
    std::vector<LyricLine> m_lines;
    bool m_synced = false;
};
