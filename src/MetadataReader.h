#pragma once

#include "ImageUtils.h"
#include "Lyrics.h"
#include "Track.h"

#include <string>

// Thin wrapper around TagLib. All functions are thread-safe and may be called
// from worker threads.
namespace Metadata {

// Tags and duration. Fields TagLib cannot provide are guessed from the path.
Track readTrack(const std::string &path);

// The folder is the album, so its cover image (cover.jpg, folder.png, ...)
// wins; embedded artwork is the fallback.
Image readCover(const std::string &path);
Image readEmbeddedCover(const std::string &path);
Image findFolderCover(const std::string &directory);
// Path of the folder's cover image file, or "" if there is none.
std::string folderCoverPath(const std::string &directory);

// A sidecar .lrc file first, then embedded synchronised (ID3v2 SYLT) and
// unsynchronised (USLT / LYRICS comment) lyrics.
Lyrics readLyrics(const std::string &path);

// Reads a text file as UTF-8, falling back to Latin-1 for old LRC files.
std::string readTextFile(const std::string &path);

} // namespace Metadata
