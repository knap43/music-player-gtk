#include "Player.h"

#include "MetadataReader.h"
#include "PlayQueue.h"

#include <gst/tag/tag.h>

#include <algorithm>
#include <cstdlib>

namespace {

// GstPlayFlags: audio only (WebM video streams are ignored), software volume.
constexpr guint kPlayFlagAudio = 1 << 1;
constexpr guint kPlayFlagSoftVolume = 1 << 4;

std::string tagString(const GstTagList *tags, const char *tag)
{
    gchar *value = nullptr;
    if (!gst_tag_list_get_string(tags, tag, &value) || !value)
        return {};
    std::string out = Text::trimmed(value);
    g_free(value);
    return out;
}

Image tagImage(const GstTagList *tags, const char *tag)
{
    GstSample *sample = nullptr;
    if (!gst_tag_list_get_sample(tags, tag, &sample) || !sample)
        return {};
    Image image;
    if (GstBuffer *buffer = gst_sample_get_buffer(sample)) {
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            image = ImageUtils::fromData(map.data, map.size);
            gst_buffer_unmap(buffer, &map);
        }
    }
    gst_sample_unref(sample);
    return image;
}

} // namespace

Player::Player(PlayQueue &queue)
    : m_queue(queue)
{
    m_pipeline = gst_element_factory_make("playbin", "amber-playbin");
    if (!m_pipeline)
        g_error("GStreamer's playbin element is missing; install gst-plugins-base");

    g_object_set(m_pipeline, "flags", kPlayFlagAudio | kPlayFlagSoftVolume, nullptr);

    // AMBER_AUDIO_SINK=fakesink (or any sink name) overrides the output; handy
    // for headless testing.
    if (const char *sinkName = std::getenv("AMBER_AUDIO_SINK"); sinkName && *sinkName) {
        if (GstElement *sink = gst_element_factory_make(sinkName, nullptr)) {
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(sink), "sync"))
                g_object_set(sink, "sync", TRUE, nullptr);
            g_object_set(m_pipeline, "audio-sink", sink, nullptr);
        }
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    m_busWatch = gst_bus_add_watch(bus, &Player::busCallback, this);
    gst_object_unref(bus);

    setVolume(m_volume);
}

Player::~Player()
{
    stopTicker();
    if (m_retrySource)
        g_source_remove(m_retrySource);
    if (m_busWatch)
        g_source_remove(m_busWatch);
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline);
}

gboolean Player::busCallback(GstBus *, GstMessage *message, gpointer self)
{
    static_cast<Player *>(self)->onMessage(message);
    return G_SOURCE_CONTINUE;
}

int64_t Player::position() const
{
    if (!hasTrack())
        return 0;
    if (m_pendingSeek >= 0)
        return m_pendingSeek;
    if (m_reachedEnd)
        return duration();
    if (m_status == Status::Stopped)
        return 0;
    gint64 ns = 0;
    if (m_prerolled && gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &ns) && ns >= 0)
        return ns / GST_MSECOND;
    return 0;
}

int64_t Player::duration() const
{
    return m_duration > 0 ? m_duration : m_track.durationMs;
}

void Player::setPipelineState(GstState state)
{
    if (gst_element_set_state(m_pipeline, state) == GST_STATE_CHANGE_FAILURE && state > GST_STATE_READY)
        onError("the audio output could not be started");
}

void Player::setStatus(Status status)
{
    if (status == Status::Playing)
        startTicker();
    else
        stopTicker();
    if (status == m_status)
        return;
    m_status = status;
    signalStatusChanged.emit(status);
    signalPositionChanged.emit(position());
}

void Player::startTicker()
{
    if (m_ticker)
        return;
    // Poll while playing so the seek bar and synced lyrics move smoothly.
    m_ticker = g_timeout_add(50, [](gpointer self) -> gboolean {
        auto *player = static_cast<Player *>(self);
        player->signalPositionChanged.emit(player->position());
        return G_SOURCE_CONTINUE;
    }, this);
}

void Player::stopTicker()
{
    if (m_ticker) {
        g_source_remove(m_ticker);
        m_ticker = 0;
    }
}

void Player::load(int row, bool autoplay)
{
    if (row < 0 || row >= m_queue.count())
        return;

    m_track = m_queue.trackAt(row);
    m_entryId = m_queue.idAt(row);
    m_reachedEnd = false;
    m_pendingSeek = -1;
    m_prerolled = false;
    m_duration = 0;

    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gchar *uri = g_filename_to_uri(m_track.path.c_str(), nullptr, nullptr);
    g_object_set(m_pipeline, "uri", uri, nullptr);
    g_free(uri);

    m_cover = Metadata::readCover(m_track.path);

    signalTrackChanged.emit(m_track);
    signalCoverChanged.emit(m_cover);
    signalLyricsChanged.emit(Metadata::readLyrics(m_track.path));
    signalDurationChanged.emit(m_track.durationMs);
    signalPositionChanged.emit(0);
    signalSeeked.emit(0);

    setPipelineState(autoplay ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    setStatus(autoplay ? Status::Playing : Status::Paused);
}

void Player::playRow(int row)
{
    if (row < 0 || row >= m_queue.count())
        return;
    m_queue.setCurrentRow(row);
    load(row, true);
}

void Player::togglePlayPause()
{
    if (isPlaying())
        pause();
    else
        play();
}

void Player::play()
{
    if (m_track.isValid() && !m_reachedEnd) {
        setPipelineState(GST_STATE_PLAYING);
        setStatus(Status::Playing);
        return;
    }

    // Nothing loaded yet, or the queue ran out: continue with whatever comes
    // next, and start over from the top if there is nothing left.
    int row = -1;
    if (!m_track.isValid() && m_queue.currentRow() >= 0)
        row = m_queue.currentRow();
    else
        row = m_queue.advance(false);
    if (row < 0 && m_queue.count() > 0) {
        row = 0;
        m_queue.setCurrentRow(row);
    }
    if (row >= 0)
        load(row, true);
}

void Player::pause()
{
    if (!m_track.isValid() || m_status != Status::Playing)
        return;
    setPipelineState(GST_STATE_PAUSED);
    setStatus(Status::Paused);
}

void Player::stop()
{
    if (!m_track.isValid())
        return;
    gst_element_set_state(m_pipeline, GST_STATE_READY);
    m_prerolled = false;
    m_pendingSeek = -1;
    setStatus(Status::Stopped);
    signalPositionChanged.emit(0);
}

void Player::next()
{
    const int row = m_queue.advance(false);
    if (row >= 0) {
        load(row, true);
    } else if (m_track.isValid()) {
        gst_element_set_state(m_pipeline, GST_STATE_READY);
        m_prerolled = false;
        m_reachedEnd = true;
        setStatus(Status::Stopped);
    }
}

void Player::previous()
{
    if (position() > 3000) {
        seek(0);
        return;
    }
    const int row = m_queue.goBack();
    if (row >= 0)
        load(row, true);
    else
        seek(0);
}

void Player::seek(int64_t positionMs)
{
    if (!m_track.isValid())
        return;
    positionMs = std::max<int64_t>(0, positionMs);
    if (duration() > 0)
        positionMs = std::min(positionMs, duration());

    if (m_reachedEnd) {
        // Seeking after the queue finished replays the last track from there.
        m_reachedEnd = false;
        m_pendingSeek = positionMs;
        setPipelineState(GST_STATE_PLAYING);
        setStatus(Status::Playing);
    } else if (!m_prerolled) {
        m_pendingSeek = positionMs;
    } else {
        doSeek(positionMs);
    }
    signalPositionChanged.emit(positionMs);
    signalSeeked.emit(positionMs);
}

void Player::doSeek(int64_t positionMs)
{
    gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
                            GstSeekFlags(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                            positionMs * GST_MSECOND);
}

void Player::setVolume(int percent)
{
    m_volume = std::clamp(percent, 0, 100);
    // Cubic curve: slider positions map to roughly even loudness steps.
    const double v = m_volume / 100.0;
    g_object_set(m_pipeline, "volume", v * v * v, nullptr);
    signalVolumeChanged.emit(m_volume);
}

void Player::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    g_object_set(m_pipeline, "mute", gboolean(muted), nullptr);
    signalMutedChanged.emit(muted);
}

void Player::restoreCurrent(int64_t positionMs)
{
    const int row = m_queue.currentRow();
    if (row < 0)
        return;
    load(row, false);
    if (positionMs > 0) {
        m_pendingSeek = positionMs;
        signalPositionChanged.emit(positionMs);
    }
}

void Player::queryDuration()
{
    gint64 ns = 0;
    if (!gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &ns) || ns <= 0)
        return;
    const int64_t ms = ns / GST_MSECOND;
    if (ms == m_duration)
        return;
    m_duration = ms;
    signalDurationChanged.emit(ms);
    if (std::llabs(m_track.durationMs - ms) > 1000) {
        m_track.durationMs = ms;
        updateQueueEntry();
    }
}

void Player::updateQueueEntry()
{
    const int row = m_queue.rowOfId(m_entryId);
    if (row >= 0)
        m_queue.updateTrack(row, m_track);
}

void Player::onMessage(GstMessage *message)
{
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ASYNC_DONE:
        if (!m_prerolled) {
            m_prerolled = true;
            m_consecutiveErrors = 0;
        }
        if (m_pendingSeek >= 0) {
            const int64_t target = m_pendingSeek;
            m_pendingSeek = -1;
            doSeek(target);
        }
        queryDuration();
        break;
    case GST_MESSAGE_DURATION_CHANGED:
        queryDuration();
        break;
    case GST_MESSAGE_EOS: {
        const int row = m_queue.advance(true);
        if (row >= 0) {
            load(row, true);
        } else {
            m_reachedEnd = true;
            gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
            setStatus(Status::Stopped);
            signalPositionChanged.emit(duration());
        }
        break;
    }
    case GST_MESSAGE_TAG: {
        GstTagList *tags = nullptr;
        gst_message_parse_tag(message, &tags);
        if (tags) {
            onTags(tags);
            gst_tag_list_unref(tags);
        }
        break;
    }
    case GST_MESSAGE_ERROR: {
        GError *error = nullptr;
        gst_message_parse_error(message, &error, nullptr);
        onError(error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
        break;
    }
    default:
        break;
    }
}

void Player::onTags(GstTagList *tags)
{
    // Fall back to the stream's own artwork (e.g. WebM with an older TagLib).
    if (!m_cover) {
        Image image = tagImage(tags, GST_TAG_IMAGE);
        if (!image)
            image = tagImage(tags, GST_TAG_PREVIEW_IMAGE);
        if (image) {
            m_cover = image;
            signalCoverChanged.emit(m_cover);
        }
    }

    if (!m_track.tagged) {
        const std::string title = tagString(tags, GST_TAG_TITLE);
        const std::string artist = tagString(tags, GST_TAG_ARTIST);
        const std::string albumArtist = tagString(tags, GST_TAG_ALBUM_ARTIST);
        if (title.empty() && artist.empty() && albumArtist.empty())
            return;
        if (!title.empty())
            m_track.title = title;
        if (!artist.empty())
            m_track.artist = artist;
        else if (!albumArtist.empty())
            m_track.artist = albumArtist;
        m_track.tagged = true;
        updateQueueEntry();
        signalTrackChanged.emit(m_track);
    }
}

void Player::onError(const std::string &message)
{
    signalError.emit("Cannot play “" + Paths::fileName(m_track.path) + "”: " + message);
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    m_prerolled = false;

    // Skip unplayable files, but give up once every entry has failed.
    if (++m_consecutiveErrors >= m_queue.count()) {
        m_consecutiveErrors = 0;
        setStatus(Status::Stopped);
        return;
    }
    if (m_retrySource)
        return;
    m_retrySource = g_idle_add([](gpointer self) -> gboolean {
        auto *player = static_cast<Player *>(self);
        player->m_retrySource = 0;
        const int row = player->m_queue.advance(false);
        if (row >= 0)
            player->load(row, true);
        else
            player->setStatus(Status::Stopped);
        return G_SOURCE_REMOVE;
    }, this);
}
