#include "CoverLyricsView.h"

#include "Theme.h"
#include "Track.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kSideMargin = 32;
constexpr double kFocusLine = 0.42; // current lyric sits at 42% of the height
constexpr double kPlainTopPadding = 56;
constexpr double kCoverMargin = 32;
constexpr float kCornerRadius = 12;
const char *const kInstrumental = "♪";

GdkRGBA rgba(float r, float g, float b, float a)
{
    return GdkRGBA{r, g, b, a};
}

GskRoundedRect rounded(const graphene_rect_t &rect, float radius)
{
    GskRoundedRect r;
    gsk_rounded_rect_init_from_rect(&r, &rect, radius);
    return r;
}

} // namespace

Texture makeTexture(const Image &image)
{
    if (!image)
        return {};
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GdkTexture *texture = gdk_texture_new_for_pixbuf(image.get());
    G_GNUC_END_IGNORE_DEPRECATIONS
    return Texture(texture, [](GdkTexture *t) { g_object_unref(t); });
}

CoverLyricsView::CoverLyricsView()
    : Glib::ObjectBase("AmberCoverLyricsView")
    , Gtk::Widget()
{
    set_hexpand(true);
    set_vexpand(true);
    set_overflow(Gtk::Overflow::HIDDEN);
    add_css_class("cover-view");

    AdwAnimationTarget *target = adw_callback_animation_target_new(
        [](double value, gpointer self) {
            auto *view = static_cast<CoverLyricsView *>(self);
            view->m_scroll = value;
            view->queue_draw();
        },
        this, nullptr);
    m_animation = adw_timed_animation_new(GTK_WIDGET(gobj()), 0, 0, 420, target);
    adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(m_animation), ADW_EASE_OUT_CUBIC);

    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    scroll->signal_scroll().connect(
        [this, scroll](double, double dy) -> bool {
            if (!lyricsVisible())
                return false;
            const bool wheel = gtk_event_controller_scroll_get_unit(scroll->gobj()) == GDK_SCROLL_UNIT_WHEEL;
            adw_animation_pause(m_animation);
            m_scroll = clampScroll(m_scroll + dy * (wheel ? 64.0 : 1.0));
            if (m_lyrics.isSynced()) {
                m_userScrolling = true;
                m_resume.disconnect();
                m_resume = Glib::signal_timeout().connect([this] {
                    resumeFollowing();
                    return false;
                }, 3500);
            }
            queue_draw();
            return true;
        },
        false);
    add_controller(scroll);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this, click](int, double x, double y) {
        if (!m_lyrics.isSynced())
            return;
        const int line = lineAtPoint(x, y);
        if (line < 0)
            return;
        m_userScrolling = false;
        m_resume.disconnect();
        click->set_state(Gtk::EventSequenceState::CLAIMED);
        signalSeekRequested.emit(m_lyrics.lines()[size_t(line)].timeMs);
    });
    add_controller(click);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        setHoverLine(m_lyrics.isSynced() ? lineAtPoint(x, y) : -1);
    });
    motion->signal_leave().connect([this] { setHoverLine(-1); });
    add_controller(motion);
}

CoverLyricsView::~CoverLyricsView()
{
    m_resume.disconnect();
    if (m_animation) {
        adw_animation_pause(m_animation);
        g_object_unref(m_animation);
    }
}

Gtk::SizeRequestMode CoverLyricsView::get_request_mode_vfunc() const
{
    return Gtk::SizeRequestMode::CONSTANT_SIZE;
}

void CoverLyricsView::measure_vfunc(Gtk::Orientation, int, int &minimum, int &natural, int &minimumBaseline,
                                    int &naturalBaseline) const
{
    minimum = 160;
    natural = 420;
    minimumBaseline = naturalBaseline = -1;
}

void CoverLyricsView::size_allocate_vfunc(int, int, int)
{
    relayout();
    if (m_lyrics.isSynced() && !m_userScrolling) {
        adw_animation_pause(m_animation);
        m_scroll = targetScrollFor(std::max(0, m_currentLine));
    } else {
        m_scroll = clampScroll(m_scroll);
    }
}

void CoverLyricsView::setCover(const Image &cover)
{
    m_cover = cover;
    m_coverTexture = makeTexture(cover);
    m_blurTexture = makeTexture(ImageUtils::blurred(cover));
    queue_draw();
}

void CoverLyricsView::setLyrics(const Lyrics &lyrics)
{
    m_lyrics = lyrics;
    m_currentLine = -1;
    m_hoverLine = -1;
    m_userScrolling = false;
    m_resume.disconnect();
    adw_animation_pause(m_animation);
    m_layoutWidth = -1;
    relayout();
    m_scroll = m_lyrics.isSynced() ? targetScrollFor(0) : clampScroll(-1e9);
    gtk_widget_set_cursor_from_name(GTK_WIDGET(gobj()), nullptr);
    queue_draw();
}

void CoverLyricsView::setShowLyrics(bool show)
{
    if (m_showLyrics == show)
        return;
    m_showLyrics = show;
    if (m_lyrics.isSynced())
        m_scroll = targetScrollFor(std::max(0, m_currentLine));
    queue_draw();
}

void CoverLyricsView::setHasTrack(bool hasTrack)
{
    m_hasTrack = hasTrack;
    queue_draw();
}

void CoverLyricsView::setPosition(int64_t positionMs)
{
    if (!m_lyrics.isSynced())
        return;
    const int line = m_lyrics.lineAt(positionMs);
    if (line == m_currentLine)
        return;
    m_currentLine = line;
    if (!m_userScrolling)
        scrollTo(targetScrollFor(std::max(0, line)), lyricsVisible() && get_mapped());
    queue_draw();
}

double CoverLyricsView::areaLeft() const
{
    return kSideMargin;
}

double CoverLyricsView::areaWidth() const
{
    return std::max(0.0, get_width() - 2 * kSideMargin);
}

void CoverLyricsView::relayout()
{
    const double width = areaWidth();
    if (std::abs(width - m_layoutWidth) < 0.5 && !m_layout.empty())
        return;
    m_layout.clear();
    m_layoutWidth = width;
    if (m_lyrics.isEmpty() || width <= 0)
        return;

    Pango::FontDescription font = get_pango_context()->get_font_description();
    font.set_absolute_size(std::clamp(get_width() / 27, 15, 26) * PANGO_SCALE);
    font.set_weight(Pango::Weight::SEMIBOLD);

    auto make = [&](const std::string &text) {
        auto layout = create_pango_layout(text);
        layout->set_font_description(font);
        layout->set_width(int(width * PANGO_SCALE));
        layout->set_wrap(Pango::WrapMode::WORD_CHAR);
        layout->set_alignment(Pango::Alignment::CENTER);
        return layout;
    };

    int unused = 0, lineHeight = 0;
    make("Ag")->get_pixel_size(unused, lineHeight);
    const double spacing = lineHeight * 0.6;

    double y = 0;
    for (const LyricLine &line : m_lyrics.lines()) {
        const std::string text = Text::trimmed(line.text);
        LineLayout l;
        l.top = y;
        if (text.empty() && !m_lyrics.isSynced()) {
            l.height = lineHeight * 0.4; // paragraph break
        } else {
            l.layout = make(text.empty() ? kInstrumental : text);
            int w = 0, h = 0;
            l.layout->get_pixel_size(w, h);
            l.height = h;
        }
        y += l.height + spacing;
        m_layout.push_back(std::move(l));
    }
}

double CoverLyricsView::contentHeight() const
{
    return m_layout.empty() ? 0 : m_layout.back().top + m_layout.back().height;
}

double CoverLyricsView::targetScrollFor(int line) const
{
    if (m_layout.empty())
        return 0;
    line = std::clamp(line, 0, int(m_layout.size()) - 1);
    const LineLayout &l = m_layout[size_t(line)];
    return l.top + l.height / 2 - get_height() * kFocusLine;
}

double CoverLyricsView::clampScroll(double value) const
{
    if (m_layout.empty())
        return 0;
    if (m_lyrics.isSynced())
        return std::clamp(value, targetScrollFor(0), targetScrollFor(int(m_layout.size()) - 1));
    // Short lyrics are centred vertically; long ones start near the top.
    const double min = std::min(-kPlainTopPadding, -(get_height() - contentHeight()) / 2);
    const double max = std::max(min, contentHeight() - get_height() + kPlainTopPadding);
    return std::clamp(value, min, max);
}

void CoverLyricsView::scrollTo(double target, bool animated)
{
    adw_animation_pause(m_animation);
    if (!animated || std::abs(target - m_scroll) < 0.5) {
        m_scroll = target;
        queue_draw();
        return;
    }
    adw_timed_animation_set_value_from(ADW_TIMED_ANIMATION(m_animation), m_scroll);
    adw_timed_animation_set_value_to(ADW_TIMED_ANIMATION(m_animation), target);
    adw_animation_reset(m_animation);
    adw_animation_play(m_animation);
}

void CoverLyricsView::resumeFollowing()
{
    m_userScrolling = false;
    if (m_lyrics.isSynced())
        scrollTo(targetScrollFor(std::max(0, m_currentLine)), true);
}

int CoverLyricsView::lineAtPoint(double x, double y) const
{
    if (!lyricsVisible() || x < areaLeft() || x > areaLeft() + areaWidth())
        return -1;
    const double contentY = y + m_scroll;
    for (size_t i = 0; i < m_layout.size(); ++i) {
        if (contentY >= m_layout[i].top - 4 && contentY <= m_layout[i].top + m_layout[i].height + 4)
            return int(i);
    }
    return -1;
}

void CoverLyricsView::setHoverLine(int line)
{
    if (line == m_hoverLine)
        return;
    m_hoverLine = line;
    gtk_widget_set_cursor_from_name(GTK_WIDGET(gobj()), line >= 0 ? "pointer" : nullptr);
    queue_draw();
}

void CoverLyricsView::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot> &snapshot)
{
    GtkSnapshot *s = snapshot->gobj();
    const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, float(get_width()), float(get_height()));
    const GskRoundedRect clip = rounded(bounds, kCornerRadius);
    gtk_snapshot_push_rounded_clip(s, &clip);
    if (lyricsVisible()) {
        relayout();
        drawBackdrop(s, 0.62f);
        drawLyrics(s);
    } else if (m_coverTexture) {
        drawBackdrop(s, 0.55f);
        drawCover(s);
    } else {
        drawPlaceholder(s);
    }
    gtk_snapshot_pop(s);
}

void CoverLyricsView::drawBackdrop(GtkSnapshot *s, float dim)
{
    const float w = float(get_width()), h = float(get_height());
    const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
    if (!m_blurTexture) {
        const GskColorStop stops[] = {{0.0f, rgba(0.19f, 0.19f, 0.20f, 1)}, {1.0f, rgba(0.10f, 0.10f, 0.11f, 1)}};
        const graphene_point_t start = GRAPHENE_POINT_INIT(0, 0), end = GRAPHENE_POINT_INIT(0, h);
        gtk_snapshot_append_linear_gradient(s, &bounds, &start, &end, stops, 2);
        return;
    }
    // Crop the blurred image to the widget's aspect ratio, trimming the edges,
    // which bleed in from the clamped border.
    const float tw = float(gdk_texture_get_width(m_blurTexture.get()));
    const float th = float(gdk_texture_get_height(m_blurTexture.get()));
    const float targetAspect = w / std::max(1.0f, h);
    float cw = tw, ch = tw / targetAspect;
    if (ch > th) {
        ch = th;
        cw = th * targetAspect;
    }
    const float inset = 1.0f / 12.0f;
    const float cx = (tw - cw) / 2 + cw * inset, cy = (th - ch) / 2 + ch * inset;
    cw *= 1 - 2 * inset;
    ch *= 1 - 2 * inset;
    const float sx = w / cw, sy = h / ch;
    const graphene_rect_t dest = GRAPHENE_RECT_INIT(-cx * sx, -cy * sy, tw * sx, th * sy);
    gtk_snapshot_append_scaled_texture(s, m_blurTexture.get(), GSK_SCALING_FILTER_LINEAR, &dest);
    const GdkRGBA shade = rgba(0, 0, 0, dim);
    gtk_snapshot_append_color(s, &shade, &bounds);
}

void CoverLyricsView::drawCover(GtkSnapshot *s)
{
    const double w = get_width(), h = get_height();
    const double available = std::max(16.0, std::min(w, h) - 2 * kCoverMargin);
    const double iw = gdk_texture_get_width(m_coverTexture.get());
    const double ih = gdk_texture_get_height(m_coverTexture.get());
    const double scale = available / std::max(iw, ih);
    const float fw = float(iw * scale), fh = float(ih * scale);
    const graphene_rect_t target = GRAPHENE_RECT_INIT(float((w - fw) / 2), float((h - fh) / 2), fw, fh);
    const GskRoundedRect shape = rounded(target, 8);

    const GdkRGBA shadow = rgba(0, 0, 0, 0.45f);
    gtk_snapshot_append_outset_shadow(s, &shape, &shadow, 0, 6, 0, 24);
    gtk_snapshot_push_rounded_clip(s, &shape);
    gtk_snapshot_append_scaled_texture(s, m_coverTexture.get(), GSK_SCALING_FILTER_TRILINEAR, &target);
    gtk_snapshot_pop(s);
}

void CoverLyricsView::drawPlaceholder(GtkSnapshot *s)
{
    const float w = float(get_width()), h = float(get_height());
    GdkRGBA fg;
    gtk_widget_get_color(GTK_WIDGET(gobj()), &fg);

    const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);
    GdkRGBA tint = fg;
    tint.alpha = 0.04f;
    gtk_snapshot_append_color(s, &tint, &bounds);

    const float side = std::max(16.0f, std::min(w, h) - 2 * float(kCoverMargin));
    const graphene_rect_t square = GRAPHENE_RECT_INIT((w - side) / 2, (h - side) / 2, side, side);
    const GskRoundedRect shape = rounded(square, 8);
    tint.alpha = 0.06f;
    gtk_snapshot_push_rounded_clip(s, &shape);
    gtk_snapshot_append_color(s, &tint, &square);
    gtk_snapshot_pop(s);

    const int glyph = std::max(16, int(side * 0.36f));
    GtkIconPaintable *icon = gtk_icon_theme_lookup_icon(
        gtk_icon_theme_get_for_display(gtk_widget_get_display(GTK_WIDGET(gobj()))), "audio-x-generic-symbolic",
        nullptr, glyph, gtk_widget_get_scale_factor(GTK_WIDGET(gobj())), GTK_TEXT_DIR_NONE,
        GtkIconLookupFlags(0));
    GdkRGBA iconColor = fg;
    iconColor.alpha = 0.25f;
    gtk_snapshot_save(s);
    const graphene_point_t at = GRAPHENE_POINT_INIT((w - glyph) / 2, (h - glyph) / 2);
    gtk_snapshot_translate(s, &at);
    gtk_symbolic_paintable_snapshot_symbolic(GTK_SYMBOLIC_PAINTABLE(icon), GDK_SNAPSHOT(s), glyph, glyph,
                                             &iconColor, 1);
    gtk_snapshot_restore(s);
    g_object_unref(icon);

    if (!m_hasTrack) {
        auto layout = create_pango_layout("Nothing playing");
        Pango::FontDescription font = get_pango_context()->get_font_description();
        font.set_absolute_size(std::clamp(int(side / 22), 12, 16) * PANGO_SCALE);
        layout->set_font_description(font);
        layout->set_width(int(side * PANGO_SCALE));
        layout->set_alignment(Pango::Alignment::CENTER);
        GdkRGBA textColor = fg;
        textColor.alpha = 0.45f;
        gtk_snapshot_save(s);
        const graphene_point_t textAt = GRAPHENE_POINT_INIT((w - side) / 2, h / 2 + glyph / 2.0f + 12);
        gtk_snapshot_translate(s, &textAt);
        gtk_snapshot_append_layout(s, layout->gobj(), &textColor);
        gtk_snapshot_restore(s);
    }
}

void CoverLyricsView::drawLyrics(GtkSnapshot *s)
{
    const float w = float(get_width()), h = float(get_height());
    const graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, w, h);

    // Fade the lyrics out towards the top and bottom edges.
    gtk_snapshot_push_mask(s, GSK_MASK_MODE_ALPHA);
    const GskColorStop stops[] = {
        {0.0f, rgba(0, 0, 0, 0)}, {0.13f, rgba(0, 0, 0, 1)}, {0.87f, rgba(0, 0, 0, 1)}, {1.0f, rgba(0, 0, 0, 0)}};
    const graphene_point_t start = GRAPHENE_POINT_INIT(0, 0), end = GRAPHENE_POINT_INIT(0, h);
    gtk_snapshot_append_linear_gradient(s, &bounds, &start, &end, stops, 4);
    gtk_snapshot_pop(s);

    const bool synced = m_lyrics.isSynced();
    const GdkRGBA accent = Theme::accentOnDark();
    for (size_t i = 0; i < m_layout.size(); ++i) {
        const LineLayout &l = m_layout[i];
        const double top = l.top - m_scroll;
        if (!l.layout || top > h || top + l.height < 0)
            continue;
        GdkRGBA color = rgba(1, 1, 1, 0.88f);
        if (synced) {
            const int line = int(i);
            if (line == m_currentLine)
                color = accent;
            else if (line == m_hoverLine)
                color.alpha = 0.92f;
            else if (line < m_currentLine)
                color.alpha = 0.37f;
            else
                color.alpha = 0.59f;
        }
        gtk_snapshot_save(s);
        const graphene_point_t at = GRAPHENE_POINT_INIT(float(areaLeft()), float(top));
        gtk_snapshot_translate(s, &at);
        gtk_snapshot_append_layout(s, l.layout->gobj(), &color);
        gtk_snapshot_restore(s);
    }

    gtk_snapshot_pop(s);
}
