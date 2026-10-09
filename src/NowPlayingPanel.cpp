#include "NowPlayingPanel.h"

#include <algorithm>

namespace {

void setupIconButton(Gtk::Button &button, const char *icon, const char *tooltip)
{
    button.set_icon_name(icon);
    button.set_tooltip_text(tooltip);
    button.add_css_class("flat");
    button.add_css_class("circular");
    button.set_valign(Gtk::Align::CENTER);
}

} // namespace

NowPlayingPanel::NowPlayingPanel()
    : Gtk::Box(Gtk::Orientation::VERTICAL, 12)
{
    set_margin_start(18);
    set_margin_end(18);
    set_margin_top(12);
    set_margin_bottom(18);
    set_size_request(340, -1);

    append(m_art);

    // Title and artist.
    for (Gtk::Label *label : {&m_title, &m_subtitle}) {
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_halign(Gtk::Align::CENTER);
        label->set_selectable(false);
    }
    m_title.add_css_class("now-title");
    m_subtitle.add_css_class("now-subtitle");
    m_subtitle.add_css_class("dim-label");
    auto *info = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    info->append(m_title);
    info->append(m_subtitle);
    append(*info);

    // Seek bar.
    m_seek.set_range(0, 1);
    m_seek.set_draw_value(false);
    m_seek.set_hexpand(true);
    m_seek.set_sensitive(false);
    m_seek.signal_change_value().connect(
        [this](Gtk::ScrollType, double value) {
            if (m_updating)
                return false;
            m_lastUserSeek = g_get_monotonic_time();
            m_position = int64_t(value);
            updateTimes();
            signalSeek.emit(int64_t(std::clamp(value, 0.0, double(m_duration))));
            return false;
        },
        false);
    for (Gtk::Label *label : {&m_elapsed, &m_total}) {
        label->add_css_class("numeric");
        label->add_css_class("caption");
        label->add_css_class("dim-label");
        label->set_width_chars(5);
    }
    m_elapsed.set_xalign(1);
    m_total.set_xalign(0);
    auto *seekRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    seekRow->append(m_elapsed);
    seekRow->append(m_seek);
    seekRow->append(m_total);
    append(*seekRow);

    // Transport.
    setupIconButton(m_shuffle, "media-playlist-shuffle-symbolic", "Shuffle (Ctrl+S)");
    setupIconButton(m_previous, "media-skip-backward-symbolic", "Previous (Ctrl+←)");
    setupIconButton(m_next, "media-skip-forward-symbolic", "Next (Ctrl+→)");
    setupIconButton(m_repeat, "media-playlist-repeat-symbolic", "Repeat: off (Ctrl+R)");
    m_play.set_icon_name("media-playback-start-symbolic");
    m_play.set_tooltip_text("Play / Pause (Space)");
    m_play.add_css_class("circular");
    m_play.add_css_class("suggested-action");
    m_play.add_css_class("play-button");
    m_play.set_valign(Gtk::Align::CENTER);

    m_shuffle.signal_toggled().connect([this] {
        if (!m_updating)
            signalShuffleToggled.emit(m_shuffle.get_active());
    });
    m_repeat.signal_toggled().connect([this] {
        if (!m_updating)
            signalRepeatClicked.emit();
    });
    m_previous.signal_clicked().connect([this] { signalPrevious.emit(); });
    m_play.signal_clicked().connect([this] { signalPlayPause.emit(); });
    m_next.signal_clicked().connect([this] { signalNext.emit(); });

    auto *transport = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    transport->add_css_class("transport");
    transport->set_halign(Gtk::Align::CENTER);
    for (Gtk::Widget *w : std::initializer_list<Gtk::Widget *>{&m_shuffle, &m_previous, &m_play, &m_next, &m_repeat})
        transport->append(*w);
    append(*transport);

    // Volume and the lyrics toggle.
    m_mute.add_css_class("flat");
    m_mute.add_css_class("circular");
    m_mute.set_tooltip_text("Mute (M)");
    m_mute.signal_toggled().connect([this] {
        if (!m_updating)
            signalMuteToggled.emit(m_mute.get_active());
    });
    m_volume.set_range(0, 100);
    m_volume.set_increments(5, 10);
    m_volume.set_draw_value(false);
    m_volume.set_size_request(120, -1);
    m_volume.set_tooltip_text("Volume (Ctrl+↑ / Ctrl+↓)");
    m_volume.signal_change_value().connect(
        [this](Gtk::ScrollType, double value) {
            if (!m_updating)
                signalVolume.emit(int(std::clamp(value, 0.0, 100.0) + 0.5));
            return false;
        },
        false);

    m_lyrics.set_label("Lyrics");
    m_lyrics.add_css_class("flat");
    m_lyrics.set_tooltip_text("Show lyrics over the cover (L)");
    m_lyrics.set_active(true);
    m_lyrics.signal_toggled().connect([this] {
        m_art.setShowLyrics(m_lyrics.get_active());
        if (!m_updating)
            signalLyricsToggled.emit(m_lyrics.get_active());
    });

    auto *bottom = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    bottom->append(m_mute);
    bottom->append(m_volume);
    auto *spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    bottom->append(*spacer);
    bottom->append(m_lyrics);
    append(*bottom);

    m_art.signalSeekRequested.connect([this](int64_t ms) { signalSeek.emit(ms); });

    clearTrack();
    setRepeat(PlayQueue::Repeat::Off);
    updateVolumeIcon();
    m_volume.set_value(m_volumeValue);
}

void NowPlayingPanel::setTrack(const Track &track)
{
    m_title.set_text(track.displayTitle());
    m_subtitle.set_text(track.displayArtist() + "  ·  " + track.displayAlbum());
    m_title.set_tooltip_text(track.displayTitle());
    m_subtitle.set_tooltip_text(track.path);
    m_art.setHasTrack(true);
    m_seek.set_sensitive(true);
}

void NowPlayingPanel::clearTrack()
{
    m_title.set_text("Amber");
    m_subtitle.set_text("Pick something from the library");
    m_title.set_tooltip_text("");
    m_subtitle.set_tooltip_text("");
    m_art.setHasTrack(false);
    m_seek.set_sensitive(false);
    setDuration(0);
    setPosition(0);
}

void NowPlayingPanel::setCover(const Image &cover)
{
    m_art.setCover(cover);
}

void NowPlayingPanel::setLyrics(const Lyrics &lyrics)
{
    m_art.setLyrics(lyrics);
}

void NowPlayingPanel::setPlaying(bool playing)
{
    m_play.set_icon_name(playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
}

void NowPlayingPanel::setPosition(int64_t positionMs)
{
    m_art.setPosition(positionMs);
    // While the user drags the seek bar, playback ticks must not move it.
    if (g_get_monotonic_time() - m_lastUserSeek < 400000)
        return;
    m_position = positionMs;
    m_updating = true;
    m_seek.set_value(double(std::min(positionMs, std::max<int64_t>(m_duration, 1))));
    m_updating = false;
    updateTimes();
}

void NowPlayingPanel::setDuration(int64_t durationMs)
{
    m_duration = std::max<int64_t>(0, durationMs);
    m_updating = true;
    m_seek.set_range(0, double(std::max<int64_t>(m_duration, 1)));
    m_updating = false;
    updateTimes();
}

void NowPlayingPanel::updateTimes()
{
    // Format::duration shows "--:--" only for 0, which is "0:00" here.
    m_elapsed.set_text(Format::duration(std::max<int64_t>(m_position, 1)));
    m_total.set_text(Format::duration(m_duration));
}

void NowPlayingPanel::setVolume(int percent)
{
    m_volumeValue = percent;
    m_updating = true;
    m_volume.set_value(percent);
    m_updating = false;
    updateVolumeIcon();
}

void NowPlayingPanel::setMuted(bool muted)
{
    m_muted = muted;
    m_updating = true;
    m_mute.set_active(muted);
    m_updating = false;
    updateVolumeIcon();
}

void NowPlayingPanel::updateVolumeIcon()
{
    const char *icon = "audio-volume-high-symbolic";
    if (m_muted || m_volumeValue == 0)
        icon = "audio-volume-muted-symbolic";
    else if (m_volumeValue < 34)
        icon = "audio-volume-low-symbolic";
    else if (m_volumeValue < 67)
        icon = "audio-volume-medium-symbolic";
    m_mute.set_icon_name(icon);
}

void NowPlayingPanel::setShuffle(bool on)
{
    m_updating = true;
    m_shuffle.set_active(on);
    m_updating = false;
}

void NowPlayingPanel::setRepeat(PlayQueue::Repeat mode)
{
    m_updating = true;
    switch (mode) {
    case PlayQueue::Repeat::Off:
        m_repeat.set_active(false);
        m_repeat.set_icon_name("media-playlist-repeat-symbolic");
        m_repeat.set_tooltip_text("Repeat: off (Ctrl+R)");
        break;
    case PlayQueue::Repeat::All:
        m_repeat.set_active(true);
        m_repeat.set_icon_name("media-playlist-repeat-symbolic");
        m_repeat.set_tooltip_text("Repeat: queue (Ctrl+R)");
        break;
    case PlayQueue::Repeat::One:
        m_repeat.set_active(true);
        m_repeat.set_icon_name("media-playlist-repeat-song-symbolic");
        m_repeat.set_tooltip_text("Repeat: track (Ctrl+R)");
        break;
    }
    m_updating = false;
}

void NowPlayingPanel::setLyricsEnabled(bool on)
{
    m_updating = true;
    m_lyrics.set_active(on);
    m_updating = false;
    m_art.setShowLyrics(on);
}
