#pragma once

#include "ImageUtils.h"
#include "Lyrics.h"
#include "Track.h"

#include <gst/gst.h>
#include <sigc++/sigc++.h>

#include <string>

class PlayQueue;

// Drives a GStreamer playbin from the play queue and gathers everything the
// now-playing panel and the MPRIS service show (artwork, lyrics, progress).
class Player
{
public:
    enum class Status { Stopped, Playing, Paused };

    explicit Player(PlayQueue &queue);
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    PlayQueue &queue() const { return m_queue; }
    Status status() const { return m_status; }
    bool isPlaying() const { return m_status == Status::Playing; }
    bool hasTrack() const { return m_track.isValid(); }
    const Track &currentTrack() const { return m_track; }
    // Queue entry id of the loaded track (0 when nothing is loaded).
    uint64_t currentEntryId() const { return m_entryId; }
    const Image &cover() const { return m_cover; }
    int64_t position() const;
    int64_t duration() const;
    bool canSeek() const { return hasTrack() && duration() > 0; }

    int volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }

    void playRow(int row);
    void togglePlayPause();
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seek(int64_t positionMs);
    void setVolume(int percent);
    void setMuted(bool muted);
    // Loads the queue's current row without starting playback, optionally
    // resuming at a saved position (used to restore the previous session).
    void restoreCurrent(int64_t positionMs);

    sigc::signal<void(const Track &)> signalTrackChanged;
    sigc::signal<void(const Image &)> signalCoverChanged;
    sigc::signal<void(const Lyrics &)> signalLyricsChanged;
    sigc::signal<void(Status)> signalStatusChanged;
    sigc::signal<void(int64_t)> signalPositionChanged;
    sigc::signal<void(int64_t)> signalDurationChanged;
    sigc::signal<void(int64_t)> signalSeeked; // explicit jumps, for MPRIS
    sigc::signal<void(int)> signalVolumeChanged;
    sigc::signal<void(bool)> signalMutedChanged;
    sigc::signal<void(const std::string &)> signalError;

private:
    static gboolean busCallback(GstBus *bus, GstMessage *message, gpointer self);
    void onMessage(GstMessage *message);
    void onTags(GstTagList *tags);
    void onError(const std::string &message);
    void load(int row, bool autoplay);
    void setStatus(Status status);
    void setPipelineState(GstState state);
    void doSeek(int64_t positionMs);
    void queryDuration();
    void updateQueueEntry();
    void startTicker();
    void stopTicker();

    PlayQueue &m_queue;
    GstElement *m_pipeline = nullptr;
    guint m_busWatch = 0;
    guint m_ticker = 0;
    guint m_retrySource = 0;

    Track m_track;
    uint64_t m_entryId = 0;
    Image m_cover;
    Status m_status = Status::Stopped;
    bool m_prerolled = false;
    bool m_reachedEnd = false; // stopped because the queue ran out
    int64_t m_pendingSeek = -1;
    int64_t m_duration = 0;
    int m_consecutiveErrors = 0;
    int m_volume = 80;
    bool m_muted = false;
};
