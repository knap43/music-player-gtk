#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Everything the player needs to know about a single audio file.
struct Track {
    std::string path;
    std::string title;
    std::string artist;
    int year = 0;
    int trackNumber = 0;
    int discNumber = 0;
    int64_t durationMs = 0;
    // False when TagLib could not read the file and the fields above were
    // guessed from the file name; the player then trusts the stream's own
    // metadata once playback starts.
    bool tagged = false;

    bool isValid() const { return !path.empty(); }
    std::string displayTitle() const;
    std::string displayArtist() const;
    // Albums are folders: this is the name of the folder holding the file,
    // whatever the album tag says.
    std::string displayAlbum() const;
};

namespace Formats {
// Lower-case file suffixes the player accepts.
const std::vector<std::string> &suffixes();
bool isSupported(const std::string &path);
} // namespace Formats

namespace Format {
// 3:07, 1:02:45; "--:--" for unknown durations.
std::string duration(int64_t ms);
} // namespace Format

namespace Paths {
std::string fileName(const std::string &path);        // "/a/b/Song.flac" -> "Song.flac"
std::string completeBaseName(const std::string &path); // -> "Song"
std::string suffix(const std::string &path);           // -> "flac" (lower case)
std::string dirPath(const std::string &path);          // -> "/a/b"
std::string dirName(const std::string &directory);     // "/a/b" -> "b"
std::string join(const std::string &dir, const std::string &name);
bool exists(const std::string &path);
bool isDir(const std::string &path);
bool isFile(const std::string &path);
} // namespace Paths

namespace Text {
std::string trimmed(const std::string &s);
std::string toLower(const std::string &s);
// Case-insensitive UTF-8 comparison key.
std::string casefold(const std::string &s);
bool startsWith(const std::string &s, const std::string &prefix);
} // namespace Text
