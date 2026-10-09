#include "Track.h"

#include <glib.h>

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

std::string Track::displayTitle() const
{
    return title.empty() ? Paths::completeBaseName(path) : title;
}

std::string Track::displayArtist() const
{
    return artist.empty() ? std::string("Unknown Artist") : artist;
}

std::string Track::displayAlbum() const
{
    return Paths::dirName(Paths::dirPath(path));
}

namespace Formats {

const std::vector<std::string> &suffixes()
{
    static const std::vector<std::string> list{"mp3", "flac", "ogg", "oga", "opus", "webm"};
    return list;
}

bool isSupported(const std::string &path)
{
    const std::string s = Paths::suffix(path);
    const auto &list = suffixes();
    return std::find(list.begin(), list.end(), s) != list.end();
}

} // namespace Formats

namespace Format {

std::string duration(int64_t ms)
{
    if (ms <= 0)
        return "--:--";
    const int64_t total = (ms + 500) / 1000;
    const int64_t hours = total / 3600;
    const int64_t minutes = (total / 60) % 60;
    const int64_t seconds = total % 60;
    char buf[32];
    if (hours > 0)
        std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", (long long)hours, (long long)minutes,
                      (long long)seconds);
    else
        std::snprintf(buf, sizeof buf, "%lld:%02lld", (long long)minutes, (long long)seconds);
    return buf;
}

} // namespace Format

namespace Paths {

std::string fileName(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string completeBaseName(const std::string &path)
{
    const std::string name = fileName(path);
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

std::string suffix(const std::string &path)
{
    const std::string name = fileName(path);
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : Text::toLower(name.substr(dot + 1));
}

std::string dirPath(const std::string &path)
{
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return ".";
    if (slash == 0)
        return "/";
    return path.substr(0, slash);
}

std::string dirName(const std::string &directory)
{
    std::string d = directory;
    while (d.size() > 1 && d.back() == '/')
        d.pop_back();
    return fileName(d);
}

std::string join(const std::string &dir, const std::string &name)
{
    if (dir.empty())
        return name;
    return dir.back() == '/' ? dir + name : dir + "/" + name;
}

bool exists(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace Paths

namespace Text {

std::string trimmed(const std::string &s)
{
    const char *ws = " \t\r\n\f\v";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos)
        return {};
    const auto last = s.find_last_not_of(ws);
    return s.substr(first, last - first + 1);
}

std::string toLower(const std::string &s)
{
    std::string out = s;
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return out;
}

std::string casefold(const std::string &s)
{
    if (!g_utf8_validate(s.data(), gssize(s.size()), nullptr))
        return toLower(s);
    gchar *folded = g_utf8_casefold(s.data(), gssize(s.size()));
    std::string out(folded);
    g_free(folded);
    return out;
}

bool startsWith(const std::string &s, const std::string &prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

} // namespace Text
