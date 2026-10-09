#pragma once

#include "ImageUtils.h"
#include "Lyrics.h"

#include <adwaita.h>
#include <gtkmm.h>

#include <memory>
#include <vector>

using Texture = std::shared_ptr<GdkTexture>;
Texture makeTexture(const Image &image);

// The large artwork area of the now-playing pane. Shows the cover over a
// blurred copy of itself, or the lyrics over the blurred cover. Synced lyrics
// follow playback; clicking a synced line seeks there.
class CoverLyricsView : public Gtk::Widget
{
public:
    CoverLyricsView();
    ~CoverLyricsView() override;

    void setCover(const Image &cover);
    void setLyrics(const Lyrics &lyrics);
    void setShowLyrics(bool show);
    bool showLyrics() const { return m_showLyrics; }
    bool hasLyrics() const { return !m_lyrics.isEmpty(); }
    void setHasTrack(bool hasTrack);
    void setPosition(int64_t positionMs);

    sigc::signal<void(int64_t)> signalSeekRequested;

protected:
    Gtk::SizeRequestMode get_request_mode_vfunc() const override;
    void measure_vfunc(Gtk::Orientation orientation, int forSize, int &minimum, int &natural,
                       int &minimumBaseline, int &naturalBaseline) const override;
    void size_allocate_vfunc(int width, int height, int baseline) override;
    void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot> &snapshot) override;

private:
    struct LineLayout {
        double top = 0;
        double height = 0;
        Glib::RefPtr<Pango::Layout> layout;
    };

    bool lyricsVisible() const { return m_showLyrics && !m_lyrics.isEmpty(); }
    double areaLeft() const;
    double areaWidth() const;
    void relayout();
    double contentHeight() const;
    double targetScrollFor(int line) const;
    double clampScroll(double value) const;
    void scrollTo(double target, bool animated);
    void resumeFollowing();
    int lineAtPoint(double x, double y) const;
    void setHoverLine(int line);

    void drawBackdrop(GtkSnapshot *s, float dim);
    void drawCover(GtkSnapshot *s);
    void drawPlaceholder(GtkSnapshot *s);
    void drawLyrics(GtkSnapshot *s);

    Image m_cover;
    Texture m_coverTexture;
    Texture m_blurTexture;
    Lyrics m_lyrics;
    std::vector<LineLayout> m_layout;
    double m_layoutWidth = -1;
    double m_scroll = 0;
    int m_currentLine = -1;
    int m_hoverLine = -1;
    bool m_showLyrics = true;
    bool m_hasTrack = false;
    bool m_userScrolling = false;

    AdwAnimation *m_animation = nullptr;
    sigc::connection m_resume;
};
