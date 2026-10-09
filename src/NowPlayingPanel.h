#pragma once

#include "CoverLyricsView.h"
#include "PlayQueue.h"
#include "Track.h"

#include <gtkmm.h>

// Artwork / lyrics, track info, seek bar and transport controls.
class NowPlayingPanel : public Gtk::Box
{
public:
    NowPlayingPanel();

    void setTrack(const Track &track);
    void clearTrack();
    void setCover(const Image &cover);
    void setLyrics(const Lyrics &lyrics);
    void setPlaying(bool playing);
    void setPosition(int64_t positionMs);
    void setDuration(int64_t durationMs);
    void setVolume(int percent);
    void setMuted(bool muted);
    void setShuffle(bool on);
    void setRepeat(PlayQueue::Repeat mode);
    void setLyricsEnabled(bool on);
    bool lyricsEnabled() const { return m_art.showLyrics(); }

    sigc::signal<void()> signalPlayPause;
    sigc::signal<void()> signalNext;
    sigc::signal<void()> signalPrevious;
    sigc::signal<void(int64_t)> signalSeek;
    sigc::signal<void(int)> signalVolume;
    sigc::signal<void(bool)> signalMuteToggled;
    sigc::signal<void(bool)> signalShuffleToggled;
    sigc::signal<void()> signalRepeatClicked;
    sigc::signal<void(bool)> signalLyricsToggled;

private:
    void updateVolumeIcon();
    void updateTimes();

    CoverLyricsView m_art;
    Gtk::Label m_title;
    Gtk::Label m_subtitle;
    Gtk::Label m_elapsed;
    Gtk::Label m_total;
    Gtk::Scale m_seek;
    Gtk::Scale m_volume;
    Gtk::ToggleButton m_shuffle;
    Gtk::Button m_previous;
    Gtk::Button m_play;
    Gtk::Button m_next;
    Gtk::ToggleButton m_repeat;
    Gtk::ToggleButton m_mute;
    Gtk::ToggleButton m_lyrics;

    bool m_updating = false;
    int64_t m_position = 0;
    int64_t m_duration = 0;
    gint64 m_lastUserSeek = 0; // monotonic µs; ticks are ignored right after
    int m_volumeValue = 80;
    bool m_muted = false;
};
