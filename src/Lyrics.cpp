#include "Lyrics.h"

#include "Track.h"

#include <algorithm>
#include <regex>

namespace {

int64_t toMs(const std::string &minutes, const std::string &seconds, const std::string &fraction)
{
    int64_t ms = std::stoll(minutes) * 60000 + std::stoll(seconds) * 1000;
    if (!fraction.empty()) {
        std::string f = fraction.substr(0, 3);
        while (f.size() < 3)
            f += '0';
        ms += std::stoll(f);
    }
    return ms;
}

bool isBlank(const std::string &s)
{
    return Text::trimmed(s).empty();
}

void trimBlankEdges(std::vector<LyricLine> &lines)
{
    while (!lines.empty() && isBlank(lines.front().text))
        lines.erase(lines.begin());
    while (!lines.empty() && isBlank(lines.back().text))
        lines.pop_back();
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> out;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r' || c == '\n') {
            out.push_back(current);
            current.clear();
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else {
            current += c;
        }
    }
    out.push_back(current);
    return out;
}

} // namespace

Lyrics Lyrics::parse(const std::string &input)
{
    static const std::regex timeTag(R"(^\s*\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    static const std::regex wordTag(R"(<\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>)");
    static const std::regex infoTag(R"(^\s*\[([A-Za-z#]+):(.*)\]\s*$)");

    std::string text = input;
    if (Text::startsWith(text, "\xEF\xBB\xBF"))
        text.erase(0, 3);

    std::vector<LyricLine> synced;
    std::vector<LyricLine> plain;
    int64_t offsetMs = 0;

    for (const std::string &raw : splitLines(text)) {
        std::string rest = raw;
        std::vector<int64_t> times;
        std::smatch m;
        while (std::regex_search(rest, m, timeTag)) {
            times.push_back(toMs(m[1].str(), m[2].str(), m[3].matched ? m[3].str() : std::string()));
            rest = m.suffix().str();
        }

        if (!times.empty()) {
            rest = Text::trimmed(std::regex_replace(rest, wordTag, ""));
            for (int64_t t : times)
                synced.push_back({t, rest});
            continue;
        }

        if (std::regex_match(raw, m, infoTag)) {
            // LRC header such as [ar:Artist] or [offset:+250]; never lyrics.
            if (Text::toLower(m[1].str()) == "offset") {
                try {
                    offsetMs = std::stoll(Text::trimmed(m[2].str()));
                } catch (...) {
                    offsetMs = 0;
                }
            }
            continue;
        }

        std::string line = raw;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        plain.push_back({-1, line});
    }

    if (!synced.empty()) {
        // A positive offset makes the lyrics appear earlier.
        for (LyricLine &line : synced)
            line.timeMs = std::max<int64_t>(0, line.timeMs - offsetMs);
        std::stable_sort(synced.begin(), synced.end(),
                         [](const LyricLine &a, const LyricLine &b) { return a.timeMs < b.timeMs; });
        return fromLines(std::move(synced), true);
    }
    return fromLines(std::move(plain), false);
}

Lyrics Lyrics::fromLines(std::vector<LyricLine> lines, bool synced)
{
    if (!synced)
        trimBlankEdges(lines);
    Lyrics lyrics;
    // Collapse runs of blank lines so paragraphs keep a single gap.
    for (const LyricLine &line : lines) {
        if (!synced && isBlank(line.text) && !lyrics.m_lines.empty() && isBlank(lyrics.m_lines.back().text))
            continue;
        lyrics.m_lines.push_back(line);
    }
    lyrics.m_synced = synced && !lyrics.m_lines.empty();

    const bool anyText = std::any_of(lyrics.m_lines.begin(), lyrics.m_lines.end(),
                                     [](const LyricLine &l) { return !isBlank(l.text); });
    return anyText ? lyrics : Lyrics();
}

int Lyrics::lineAt(int64_t positionMs) const
{
    if (!m_synced || m_lines.empty())
        return -1;
    const auto it = std::upper_bound(m_lines.cbegin(), m_lines.cend(), positionMs,
                                     [](int64_t pos, const LyricLine &line) { return pos < line.timeMs; });
    return int(it - m_lines.cbegin()) - 1;
}
