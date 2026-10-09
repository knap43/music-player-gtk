#include "Library.h"

#include "MainThread.h"
#include "MetadataReader.h"
#include "NaturalSort.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace {

constexpr uint32_t kCacheMagic = 0x414d4247; // "AMBG"
constexpr uint32_t kCacheVersion = 1;
constexpr int kThumbnailSize = 128;

struct CacheEntry {
    int64_t mtime = 0;
    int64_t size = 0;
    Track track;
};

// --- tiny binary serialisation for the tag cache ---------------------------

template <typename T>
void put(std::ostream &out, T value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof value);
}

void putString(std::ostream &out, const std::string &s)
{
    put<uint32_t>(out, uint32_t(s.size()));
    out.write(s.data(), std::streamsize(s.size()));
}

template <typename T>
bool get(std::istream &in, T &value)
{
    return bool(in.read(reinterpret_cast<char *>(&value), sizeof value));
}

bool getString(std::istream &in, std::string &s)
{
    uint32_t n = 0;
    if (!get(in, n) || n > (1u << 20))
        return false;
    s.resize(n);
    return n == 0 || bool(in.read(&s[0], n));
}

std::string tagCachePath()
{
    return Library::cacheDir() + "/library.cache";
}

std::unordered_map<std::string, CacheEntry> loadTagCache()
{
    std::unordered_map<std::string, CacheEntry> cache;
    std::ifstream in(tagCachePath(), std::ios::binary);
    if (!in)
        return cache;
    uint32_t magic = 0, version = 0, count = 0;
    if (!get(in, magic) || !get(in, version) || !get(in, count) || magic != kCacheMagic
        || version != kCacheVersion)
        return cache;
    cache.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        CacheEntry e;
        int32_t year = 0, track = 0, disc = 0;
        uint8_t tagged = 0;
        if (!get(in, e.mtime) || !get(in, e.size) || !getString(in, e.track.path)
            || !getString(in, e.track.title) || !getString(in, e.track.artist) || !get(in, year)
            || !get(in, track) || !get(in, disc) || !get(in, e.track.durationMs) || !get(in, tagged)) {
            cache.clear();
            return cache;
        }
        e.track.year = year;
        e.track.trackNumber = track;
        e.track.discNumber = disc;
        e.track.tagged = tagged != 0;
        cache.emplace(e.track.path, std::move(e));
    }
    return cache;
}

void saveTagCache(const std::unordered_map<std::string, CacheEntry> &cache)
{
    g_mkdir_with_parents(Library::cacheDir().c_str(), 0755);
    const std::string path = tagCachePath();
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        put<uint32_t>(out, kCacheMagic);
        put<uint32_t>(out, kCacheVersion);
        put<uint32_t>(out, uint32_t(cache.size()));
        for (const auto &[key, e] : cache) {
            put(out, e.mtime);
            put(out, e.size);
            putString(out, e.track.path);
            putString(out, e.track.title);
            putString(out, e.track.artist);
            put<int32_t>(out, e.track.year);
            put<int32_t>(out, e.track.trackNumber);
            put<int32_t>(out, e.track.discNumber);
            put(out, e.track.durationMs);
            put<uint8_t>(out, e.track.tagged ? 1 : 0);
        }
        if (!out)
            return;
    }
    std::rename(temp.c_str(), path.c_str());
}

int64_t mtimeMs(const struct stat &st)
{
    return int64_t(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
}

int64_t mtimeOf(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? mtimeMs(st) : 0;
}

std::string thumbnailCachePath(const std::string &albumKey, const std::string &firstTrack)
{
    // The album key is the folder; its timestamp changes when a cover image is
    // added, removed or renamed there.
    const std::string material = "folder-albums\n" + albumKey + "\n" + std::to_string(mtimeOf(albumKey))
                                 + "\n" + firstTrack + "\n" + std::to_string(mtimeOf(firstTrack));
    gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, material.c_str(), gssize(material.size()));
    std::string out = Library::cacheDir() + "/thumbs/" + hash;
    g_free(hash);
    return out;
}

// Walks a directory tree, following symlinks but never visiting a directory twice.
template <typename Fn>
bool walk(const std::string &dir, std::set<std::pair<dev_t, ino_t>> &visited,
          const std::atomic_bool *cancel, Fn &&onFile)
{
    struct stat st;
    if (::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        return true;
    if (!visited.insert({st.st_dev, st.st_ino}).second)
        return true;
    GDir *handle = g_dir_open(dir.c_str(), 0, nullptr);
    if (!handle)
        return true;
    std::vector<std::string> subdirs;
    while (const gchar *name = g_dir_read_name(handle)) {
        if (cancel && cancel->load()) {
            g_dir_close(handle);
            return false;
        }
        const std::string path = Paths::join(dir, name);
        struct stat entry;
        if (::stat(path.c_str(), &entry) != 0)
            continue;
        if (S_ISDIR(entry.st_mode))
            subdirs.push_back(path);
        else if (S_ISREG(entry.st_mode) && Formats::isSupported(path) && ::access(path.c_str(), R_OK) == 0)
            onFile(path, entry);
    }
    g_dir_close(handle);
    for (const std::string &sub : subdirs)
        if (!walk(sub, visited, cancel, onFile))
            return false;
    return true;
}

std::string absolutePath(const std::string &path)
{
    gchar *canon = g_canonicalize_filename(path.c_str(), nullptr);
    std::string out(canon);
    g_free(canon);
    while (out.size() > 1 && out.back() == '/')
        out.pop_back();
    return out;
}

// Path of `path` relative to `root`, "." for the root itself, or "" if outside.
std::string relativeTo(const std::string &root, const std::string &path)
{
    if (path == root)
        return ".";
    const std::string prefix = root == "/" ? root : root + "/";
    if (Text::startsWith(path, prefix))
        return path.substr(prefix.size());
    return {};
}

} // namespace

int64_t Album::durationMs() const
{
    int64_t total = 0;
    for (const Track &t : tracks)
        total += t.durationMs;
    return total;
}

Library::Library() = default;

Library::~Library()
{
    *m_alive = false;
    cancelWork();
    if (m_scanThread.joinable())
        m_scanThread.join();
    if (m_thumbThread.joinable())
        m_thumbThread.join();
}

std::string Library::cacheDir()
{
    return std::string(g_get_user_cache_dir()) + "/amber-player";
}

Track Library::track(const std::string &path) const
{
    const auto it = m_byPath.find(path);
    if (it != m_byPath.end())
        return it->second;
    return Metadata::readTrack(path);
}

std::vector<std::string> Library::collectFiles(const std::string &directory)
{
    std::vector<std::string> files;
    std::set<std::pair<dev_t, ino_t>> visited;
    walk(absolutePath(directory), visited, nullptr,
         [&](const std::string &path, const struct stat &) { files.push_back(path); });
    std::sort(files.begin(), files.end(), NaturalSort::less);
    return files;
}

void Library::cancelWork()
{
    if (m_cancel)
        m_cancel->store(true);
}

void Library::scan(const std::string &rootPath)
{
    if (rootPath.empty())
        return;
    cancelWork();
    if (m_scanThread.joinable())
        m_scanThread.join();
    if (m_thumbThread.joinable())
        m_thumbThread.join();

    m_root = absolutePath(rootPath);
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_scanning = true;
    const uint64_t generation = ++m_generation;
    signalScanStarted.emit();

    m_scanThread = std::thread([this, root = m_root, cancel = m_cancel, generation,
                                alive = std::weak_ptr<bool>(m_alive)] {
        ScanResult result;
        result.root = root;

        const auto oldCache = loadTagCache();
        auto newCache = oldCache; // entries of other roots survive
        std::vector<Track> tracks;
        int seen = 0;
        std::set<std::pair<dev_t, ino_t>> visited;

        const bool finished = walk(root, visited, cancel.get(), [&](const std::string &path, const struct stat &st) {
            const int64_t mtime = mtimeMs(st);
            const int64_t size = int64_t(st.st_size);
            const auto cached = oldCache.find(path);
            if (cached != oldCache.end() && cached->second.mtime == mtime && cached->second.size == size) {
                tracks.push_back(cached->second.track);
            } else {
                CacheEntry entry{mtime, size, Metadata::readTrack(path)};
                tracks.push_back(entry.track);
                newCache[path] = std::move(entry);
            }
            if (++seen % 25 == 0) {
                MainThread::post([this, seen, generation, alive] {
                    if (!alive.expired() && generation == m_generation)
                        signalScanProgress.emit(seen);
                });
            }
        });
        if (!finished || cancel->load())
            return;

        // Drop cache entries for files below this root that no longer exist.
        const std::string prefix = root == "/" ? root : root + "/";
        for (auto it = newCache.begin(); it != newCache.end();) {
            if (Text::startsWith(it->first, prefix) && !Paths::exists(it->first))
                it = newCache.erase(it);
            else
                ++it;
        }
        saveTagCache(newCache);

        // One album per folder.
        std::unordered_map<std::string, size_t> indexByFolder;
        for (const Track &t : tracks) {
            const std::string folder = Paths::dirPath(t.path);
            auto found = indexByFolder.find(folder);
            if (found == indexByFolder.end()) {
                Album album;
                album.key = folder;
                album.title = Paths::dirName(folder);
                // Where the folder sits, e.g. "Artist" for Artist/Album; the
                // root folder's own name for albums at the top level.
                const std::string parent = relativeTo(root, Paths::dirPath(folder));
                album.location = (parent.empty() || parent == "." || folder == root) ? Paths::dirName(root) : parent;
                found = indexByFolder.emplace(folder, result.albums.size()).first;
                result.albums.push_back(std::move(album));
            }
            result.albums[found->second].tracks.push_back(t);
        }

        for (Album &album : result.albums) {
            std::sort(album.tracks.begin(), album.tracks.end(), [](const Track &a, const Track &b) {
                return NaturalSort::less(Paths::fileName(a.path), Paths::fileName(b.path));
            });
        }
        // Folder order: Artist/Album layouts end up sorted by artist, then album.
        std::sort(result.albums.begin(), result.albums.end(), [&](const Album &a, const Album &b) {
            return NaturalSort::less(relativeTo(root, a.key), relativeTo(root, b.key));
        });

        MainThread::post([this, result = std::move(result), generation, alive]() mutable {
            if (!alive.expired())
                onScanFinished(std::move(result), generation);
        });
    });
}

void Library::rescan()
{
    scan(m_root);
}

void Library::onScanFinished(ScanResult result, uint64_t generation)
{
    if (generation != m_generation || (m_cancel && m_cancel->load()))
        return; // superseded by a newer scan

    if (m_scanThread.joinable())
        m_scanThread.join();

    m_albums = std::move(result.albums);
    m_byPath.clear();
    std::vector<ThumbJob> jobs;
    jobs.reserve(m_albums.size());
    for (const Album &album : m_albums) {
        ThumbJob job{album.key, {}};
        for (const Track &t : album.tracks) {
            m_byPath.emplace(t.path, t);
            if (job.paths.size() < 3)
                job.paths.push_back(t.path);
        }
        jobs.push_back(std::move(job));
    }
    m_scanning = false;
    signalScanFinished.emit();

    m_thumbThread = std::thread([this, jobs = std::move(jobs), cancel = m_cancel, generation,
                                 alive = std::weak_ptr<bool>(m_alive)] {
        g_mkdir_with_parents((cacheDir() + "/thumbs").c_str(), 0755);
        for (const ThumbJob &job : jobs) {
            if (cancel->load())
                return;
            if (job.paths.empty())
                continue;

            const std::string cached = thumbnailCachePath(job.key, job.paths.front());
            Image thumb;
            if (Paths::exists(cached + ".none")) {
                continue;
            } else if (Paths::exists(cached + ".jpg")) {
                thumb = ImageUtils::fromFile(cached + ".jpg");
            } else {
                Image cover = Metadata::findFolderCover(job.key);
                for (size_t i = 0; !cover && i < job.paths.size(); ++i)
                    cover = Metadata::readEmbeddedCover(job.paths[i]);
                thumb = ImageUtils::squareThumbnail(cover, kThumbnailSize);
                if (!thumb) {
                    if (FILE *f = std::fopen((cached + ".none").c_str(), "wb"))
                        std::fclose(f);
                } else {
                    ImageUtils::saveJpeg(thumb, cached + ".jpg", 90);
                }
            }

            if (thumb) {
                MainThread::post([this, key = job.key, thumb, generation, alive] {
                    if (!alive.expired() && generation == m_generation)
                        signalThumbnailReady.emit(key, thumb);
                });
            }
        }
    });
}
