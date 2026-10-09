#include "MetadataReader.h"


#include <glib.h>

#include <attachedpictureframe.h>
#include <fileref.h>
#include <flacfile.h>
#include <flacpicture.h>
#include <id3v2tag.h>
#include <mpegfile.h>
#include <synchronizedlyricsframe.h>
#include <tag.h>
#include <taglib.h>
#include <tpropertymap.h>

#include <algorithm>
#include <regex>
#include <vector>

namespace {

std::string toStd(const TagLib::String &s)
{
    return Text::trimmed(s.to8Bit(true));
}

std::string firstProperty(const TagLib::PropertyMap &props, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        if (props.contains(key)) {
            const TagLib::StringList &values = props[key];
            if (!values.isEmpty())
                return toStd(values.front());
        }
    }
    return {};
}

// "3/12" -> 3
int leadingNumber(const std::string &value)
{
    size_t i = 0;
    while (i < value.size() && (value[i] == ' ' || value[i] == '\t'))
        ++i;
    int n = 0;
    bool any = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
        n = n * 10 + (value[i] - '0');
        any = true;
        ++i;
    }
    return any ? n : 0;
}

// Fill whatever the tags did not provide from the file name.
void applyFallbacks(Track &t)
{
    if (!t.title.empty())
        return;
    std::string base = Paths::completeBaseName(t.path);
    static const std::regex numbered(R"(^(\d{1,3})[\s._-]+(.+)$)");
    std::smatch m;
    if (std::regex_match(base, m, numbered)) {
        if (t.trackNumber == 0)
            t.trackNumber = std::stoi(m[1].str());
        base = m[2].str();
    }
    const auto dash = base.find(" - ");
    if (dash != std::string::npos && dash > 0 && t.artist.empty()) {
        t.artist = Text::trimmed(base.substr(0, dash));
        base = Text::trimmed(base.substr(dash + 3));
    }
    t.title = base;
}

Image imageFromVector(const TagLib::ByteVector &v)
{
    return ImageUtils::fromData(v.data(), v.size());
}

#if TAGLIB_MAJOR_VERSION >= 2
// TagLib 2 exposes pictures of every format (including Matroska/WebM from
// 2.2 on) through the generic "PICTURE" complex property.
Image genericCover(const std::string &path)
{
    const TagLib::FileRef ref(path.c_str(), false);
    if (ref.isNull())
        return {};
    TagLib::ByteVector fallback;
    const TagLib::List<TagLib::VariantMap> pictures = ref.complexProperties("PICTURE");
    for (const TagLib::VariantMap &picture : pictures) {
        const TagLib::ByteVector data = picture.value("data").toByteVector();
        if (data.isEmpty())
            continue;
        const std::string type = Text::toLower(toStd(picture.value("pictureType").toString()));
        if (type.empty() || type == "front cover") {
            if (Image image = imageFromVector(data))
                return image;
        }
        if (fallback.isEmpty())
            fallback = data;
    }
    return imageFromVector(fallback);
}
#endif

Image mpegCover(const std::string &path)
{
    TagLib::MPEG::File file(path.c_str(), false);
    if (!file.isValid() || !file.hasID3v2Tag())
        return {};
    const TagLib::ID3v2::FrameList frames = file.ID3v2Tag()->frameListMap()["APIC"];
    TagLib::ByteVector fallback;
    for (TagLib::ID3v2::Frame *frame : frames) {
        auto *pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frame);
        if (!pic)
            continue;
        if (pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
            if (Image image = imageFromVector(pic->picture()))
                return image;
        }
        if (fallback.isEmpty())
            fallback = pic->picture();
    }
    return imageFromVector(fallback);
}

Image flacCover(const std::string &path)
{
    TagLib::FLAC::File file(path.c_str(), false);
    if (!file.isValid())
        return {};
    TagLib::ByteVector fallback;
    const TagLib::List<TagLib::FLAC::Picture *> pictures = file.pictureList();
    for (TagLib::FLAC::Picture *pic : pictures) {
        if (pic->type() == TagLib::FLAC::Picture::FrontCover) {
            if (Image image = imageFromVector(pic->data()))
                return image;
        }
        if (fallback.isEmpty())
            fallback = pic->data();
    }
    return imageFromVector(fallback);
}

Lyrics mpegSynchronisedLyrics(const std::string &path)
{
    TagLib::MPEG::File file(path.c_str(), false);
    if (!file.isValid() || !file.hasID3v2Tag())
        return {};
    const TagLib::ID3v2::FrameList frames = file.ID3v2Tag()->frameListMap()["SYLT"];
    for (TagLib::ID3v2::Frame *frame : frames) {
        auto *sylt = dynamic_cast<TagLib::ID3v2::SynchronizedLyricsFrame *>(frame);
        if (!sylt || sylt->timestampFormat() != TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds)
            continue;
        std::vector<LyricLine> lines;
        for (const auto &entry : sylt->synchedText())
            lines.push_back({int64_t(entry.time), toStd(entry.text)});
        const Lyrics lyrics = Lyrics::fromLines(lines, true);
        if (!lyrics.isEmpty())
            return lyrics;
    }
    return {};
}

bool isImageName(const std::string &name)
{
    const std::string s = Paths::suffix(name);
    return s == "jpg" || s == "jpeg" || s == "png" || s == "webp" || s == "bmp";
}

} // namespace

namespace Metadata {

Track readTrack(const std::string &path)
{
    Track t;
    t.path = path;

    const TagLib::FileRef ref(path.c_str(), true, TagLib::AudioProperties::Fast);
    if (!ref.isNull()) {
        if (TagLib::Tag *tag = ref.tag()) {
            t.title = toStd(tag->title());
            t.artist = toStd(tag->artist());
            t.year = int(tag->year());
            t.trackNumber = int(tag->track());
            t.tagged = !t.title.empty() || !t.artist.empty();
        }
        const TagLib::PropertyMap props = ref.file()->properties();
        t.discNumber = leadingNumber(firstProperty(props, {"DISCNUMBER"}));
        if (t.trackNumber == 0)
            t.trackNumber = leadingNumber(firstProperty(props, {"TRACKNUMBER"}));
        if (TagLib::AudioProperties *audio = ref.audioProperties())
            t.durationMs = audio->lengthInMilliseconds();
    }

    applyFallbacks(t);
    return t;
}

Image readEmbeddedCover(const std::string &path)
{
    const std::string suffix = Paths::suffix(path);
    Image image;
    if (suffix == "mp3")
        image = mpegCover(path);
    else if (suffix == "flac")
        image = flacCover(path);
#if TAGLIB_MAJOR_VERSION >= 2
    if (!image)
        image = genericCover(path);
#endif
    return image;
}

std::string folderCoverPath(const std::string &directory)
{
    GDir *dir = g_dir_open(directory.c_str(), 0, nullptr);
    if (!dir)
        return {};
    std::vector<std::string> images;
    while (const gchar *name = g_dir_read_name(dir)) {
        if (isImageName(name) && Paths::isFile(Paths::join(directory, name)))
            images.emplace_back(name);
    }
    g_dir_close(dir);
    if (images.empty())
        return {};
    std::sort(images.begin(), images.end());

    // Prefer the conventional names, then anything that looks like a cover.
    static const char *const preferred[] = {"cover", "folder", "front", "album", "albumart"};
    std::string best;
    int bestScore = -1;
    for (const std::string &name : images) {
        const std::string base = Text::casefold(Paths::completeBaseName(name));
        int score = 0;
        for (int i = 0; i < 5; ++i) {
            if (base == preferred[i]) {
                score = 100 - i;
                break;
            }
            if (base.find(preferred[i]) != std::string::npos)
                score = std::max(score, 50 - i);
        }
        if (base.find("back") != std::string::npos || base.find("cd") != std::string::npos)
            score -= 20;
        if (score > bestScore) {
            bestScore = score;
            best = name;
        }
    }
    return Paths::join(directory, best);
}

Image findFolderCover(const std::string &directory)
{
    const std::string path = folderCoverPath(directory);
    return path.empty() ? Image() : ImageUtils::fromFile(path);
}

Image readCover(const std::string &path)
{
    Image image = findFolderCover(Paths::dirPath(path));
    if (!image)
        image = readEmbeddedCover(path);
    return image;
}

std::string readTextFile(const std::string &path)
{
    gchar *contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(path.c_str(), &contents, &length, nullptr))
        return {};
    std::string text(contents, length);
    g_free(contents);
    if (g_utf8_validate(text.data(), gssize(text.size()), nullptr))
        return text;
    // Old LRC files are often Latin-1.
    gchar *converted = g_convert(text.data(), gssize(text.size()), "UTF-8", "ISO-8859-1", nullptr,
                                 nullptr, nullptr);
    if (!converted)
        return {};
    std::string out(converted);
    g_free(converted);
    return out;
}

Lyrics readLyrics(const std::string &path)
{
    const std::string dir = Paths::dirPath(path);
    const std::string base = Paths::completeBaseName(path);
    for (const char *suffix : {".lrc", ".LRC"}) {
        const std::string sidecar = Paths::join(dir, base + suffix);
        if (Paths::isFile(sidecar)) {
            const Lyrics lyrics = Lyrics::parse(readTextFile(sidecar));
            if (!lyrics.isEmpty())
                return lyrics;
        }
    }

    if (Paths::suffix(path) == "mp3") {
        const Lyrics synced = mpegSynchronisedLyrics(path);
        if (!synced.isEmpty())
            return synced;
    }

    const TagLib::FileRef ref(path.c_str(), false);
    if (ref.isNull())
        return {};
    const TagLib::PropertyMap props = ref.file()->properties();
    // USLT frames map to "LYRICS" or "LYRICS:<description>".
    std::string text = firstProperty(props, {"LYRICS", "UNSYNCEDLYRICS", "UNSYNCED LYRICS"});
    if (text.empty()) {
        for (auto it = props.begin(); it != props.end(); ++it) {
            if (Text::startsWith(toStd(it->first), "LYRICS") && !it->second.isEmpty()) {
                text = toStd(it->second.front());
                break;
            }
        }
    }
    return Lyrics::parse(text);
}

} // namespace Metadata
