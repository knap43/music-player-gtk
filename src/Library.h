#pragma once

#include "ImageUtils.h"
#include "Track.h"

#include <sigc++/sigc++.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// An album is a folder that directly contains audio files. Tags play no part
// in deciding what belongs together, what the album is called, or the order
// of its tracks.
struct Album {
    std::string key;      // absolute folder path
    std::string title;    // folder name
    std::string location; // parent folder, relative to the library root
    std::vector<Track> tracks; // in natural file-name order

    int64_t durationMs() const;
};

// Scans a music folder in the background; every folder with audio files in it
// becomes an album.
// Tags are cached on disk (keyed by modification time and size), so rescans
// only have to read files that changed; album thumbnails are cached too.
class Library
{
public:
    Library();
    ~Library();

    const std::string &rootPath() const { return m_root; }
    bool isScanning() const { return m_scanning; }

    const std::vector<Album> &albums() const { return m_albums; }
    int trackCount() const { return int(m_byPath.size()); }
    // Looks the track up in the library, falling back to reading its tags.
    Track track(const std::string &path) const;

    // Recursively collects supported files below a directory, naturally sorted.
    static std::vector<std::string> collectFiles(const std::string &directory);
    static std::string cacheDir();

    void scan(const std::string &rootPath);
    void rescan();

    sigc::signal<void()> signalScanStarted;
    sigc::signal<void(int)> signalScanProgress;
    sigc::signal<void()> signalScanFinished;
    sigc::signal<void(const std::string &, const Image &)> signalThumbnailReady;

private:
    struct ScanResult {
        std::string root;
        std::vector<Album> albums;
    };
    struct ThumbJob {
        std::string key;
        std::vector<std::string> paths;
    };

    void cancelWork();
    void onScanFinished(ScanResult result, uint64_t generation);

    std::string m_root;
    std::vector<Album> m_albums;
    std::unordered_map<std::string, Track> m_byPath;
    bool m_scanning = false;

    std::thread m_scanThread;
    std::thread m_thumbThread;
    std::shared_ptr<std::atomic_bool> m_cancel;
    uint64_t m_generation = 0;
    // Expires with the Library, so late callbacks from workers are dropped.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
