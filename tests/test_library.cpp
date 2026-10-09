#include "Library.h"
#include "check.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <string>

namespace {

std::string bigEndian32(uint32_t v)
{
    return {char(v >> 24), char(v >> 16), char(v >> 8), char(v)};
}

std::string textFrame(const char *id, const std::string &latin1)
{
    const std::string body = std::string(1, '\0') + latin1; // encoding: ISO-8859-1
    return std::string(id) + bigEndian32(uint32_t(body.size())) + std::string(2, '\0') + body;
}

void writeFile(const std::string &path, const std::string &data)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    CHECK(f != nullptr);
    if (!f)
        return;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

// A tiny but valid MP3: an ID3v2.3 tag followed by a few silent MPEG frames.
void writeMp3(const std::string &path, const std::string &title, const std::string &album)
{
    const std::string frames = textFrame("TIT2", title) + textFrame("TALB", album) + textFrame("TPE1", "Tagged Artist");
    const uint32_t size = uint32_t(frames.size());
    std::string tag("ID3\x03\x00\x00", 6);
    for (int shift : {21, 14, 7, 0}) // syncsafe integer
        tag += char((size >> shift) & 0x7f);
    tag += frames;
    // MPEG-1 Layer III, 128 kbit/s, 44.1 kHz: 417-byte frames.
    std::string audio;
    for (int i = 0; i < 8; ++i)
        audio += std::string("\xff\xfb\x90\x64", 4) + std::string(413, '\0');
    writeFile(path, tag + audio);
}

std::string relative(const std::string &root, const std::string &path)
{
    return path == root ? "." : path.substr(root.size() + 1);
}

} // namespace

TEST(foldersAreAlbums)
{
    gchar *tmp = g_dir_make_tmp("amber-test-XXXXXX", nullptr);
    const std::string root = tmp;
    g_free(tmp);
    g_setenv("XDG_CACHE_HOME", (root + "/.cache-home").c_str(), TRUE);

    for (const char *sub : {"Artist A/Same Name", "Artist B/Same Name", "Mixed/CD2", "Empty"})
        g_mkdir_with_parents((root + "/" + sub).c_str(), 0755);
    // Identical album tags in two folders: still two albums.
    writeMp3(root + "/Artist A/Same Name/1.mp3", "One", "Shared Tag");
    writeMp3(root + "/Artist B/Same Name/1.mp3", "One", "Shared Tag");
    // Different album tags in one folder: still one album, in file-name order.
    writeMp3(root + "/Mixed/10 - ten.mp3", "Ten", "Album X");
    writeMp3(root + "/Mixed/2 - two.mp3", "Two", "Album Y");
    writeFile(root + "/Mixed/1 - one.flac", "");
    // A sub-folder is an album of its own.
    writeFile(root + "/Mixed/CD2/a.webm", "");
    writeFile(root + "/Mixed/CD2/b.ogg", "");
    writeFile(root + "/Mixed/CD2/c.opus", "");
    // Files directly in the root form an album too; other files are ignored.
    writeFile(root + "/loose.mp3", "");
    writeFile(root + "/Empty/notes.txt", "");

    Library library;
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    bool finished = false;
    library.signalScanFinished.connect([&] {
        finished = true;
        g_main_loop_quit(loop);
    });
    g_timeout_add_seconds(10, [](gpointer l) -> gboolean { g_main_loop_quit(static_cast<GMainLoop *>(l)); return G_SOURCE_REMOVE; }, loop);
    library.scan(root);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    CHECK(finished);

    const auto &albums = library.albums();
    std::vector<std::string> keys;
    for (const Album &a : albums)
        keys.push_back(relative(root, a.key));
    const std::vector<std::string> expected{".", "Artist A/Same Name", "Artist B/Same Name", "Mixed", "Mixed/CD2"};
    CHECK(keys == expected);
    if (keys != expected)
        return;

    CHECK_EQ(albums[1].title, std::string("Same Name"));
    CHECK_EQ(albums[1].location, std::string("Artist A"));
    CHECK_EQ(albums[2].location, std::string("Artist B"));
    CHECK_EQ(albums[0].title, Paths::dirName(root));
    CHECK_EQ(albums[4].location, std::string("Mixed"));
    CHECK_EQ(albums[4].tracks.size(), 3u);

    const Album &mixed = albums[3];
    CHECK_EQ(mixed.title, std::string("Mixed"));
    CHECK_EQ(mixed.tracks.size(), 3u);
    CHECK_EQ(Paths::fileName(mixed.tracks[0].path), std::string("1 - one.flac"));
    CHECK_EQ(Paths::fileName(mixed.tracks[1].path), std::string("2 - two.mp3"));
    CHECK_EQ(Paths::fileName(mixed.tracks[2].path), std::string("10 - ten.mp3"));
    // Tags are still used for the track itself, never for the album.
    CHECK_EQ(mixed.tracks[1].title, std::string("Two"));
    CHECK_EQ(mixed.tracks[1].artist, std::string("Tagged Artist"));
    CHECK_EQ(mixed.tracks[1].displayAlbum(), std::string("Mixed"));
    CHECK_EQ(mixed.tracks[2].displayAlbum(), std::string("Mixed"));
    // Untagged files fall back to the file name.
    CHECK_EQ(mixed.tracks[0].title, std::string("one"));
    CHECK_EQ(mixed.tracks[0].trackNumber, 1);
}

int main()
{
    return check::run();
}
