#include "Theme.h"

#include <adwaita.h>
#include <gtk/gtk.h>

#include <string>

namespace {

const char *const kAccent = "#e1a34f";
// Accent used for text and icons: lighter on dark backgrounds, darker on light
// ones, so that it stays readable.
const char *const kAccentTextDark = "#ebb76c";
const char *const kAccentTextLight = "#93601b";
const char *const kAccentForeground = "rgba(0, 0, 0, 0.85)";

const char *const kStyle = R"(
.now-title { font-weight: 800; font-size: 1.35em; }
.now-subtitle { font-size: 1.05em; }
.play-button { min-width: 56px; min-height: 56px; padding: 0; }
.play-button image { -gtk-icon-size: 24px; }
.transport button.flat { min-width: 40px; min-height: 40px; }
.queue-row { padding: 6px 4px; }
.queue-row .indicator { min-width: 16px; }
.queue-row.current .title { font-weight: bold; }
.queue-heading { font-weight: bold; }
listview.queue row:drop(active) { box-shadow: inset 0 2px alpha(currentColor, 0.5); }
.library-row-thumb { border-radius: 6px; }
)";

GtkCssProvider *g_accentProvider = nullptr;

bool usesCssVariables()
{
    // libadwaita 1.6 moved its colours to CSS custom properties.
    return adw_get_major_version() > 1 || adw_get_minor_version() >= 6;
}

void loadCss(GtkCssProvider *provider, const std::string &css)
{
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_css_provider_load_from_string(provider, css.c_str());
#else
    gtk_css_provider_load_from_data(provider, css.c_str(), gssize(css.size()));
#endif
}

void updateAccent()
{
    const bool dark = adw_style_manager_get_dark(adw_style_manager_get_default());
    const std::string text = dark ? kAccentTextDark : kAccentTextLight;
    std::string css;
    if (usesCssVariables()) {
        css = std::string(":root { --accent-bg-color: ") + kAccent + "; --accent-fg-color: " + kAccentForeground
              + "; --accent-color: " + text + "; }\n";
    } else {
        css = std::string("@define-color accent_bg_color ") + kAccent + ";\n@define-color accent_fg_color "
              + kAccentForeground + ";\n@define-color accent_color " + text + ";\n";
    }
    loadCss(g_accentProvider, css);
}

} // namespace

namespace Theme {

void install()
{
    GdkDisplay *display = gdk_display_get_default();
    if (!display)
        return;

    GtkCssProvider *style = gtk_css_provider_new();
    loadCss(style, kStyle);
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(style),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(style);

    g_accentProvider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(g_accentProvider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    updateAccent();
    g_signal_connect(adw_style_manager_get_default(), "notify::dark", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer) {
                         updateAccent();
                     }),
                     nullptr);
}

GdkRGBA accentOnDark()
{
    GdkRGBA color;
    gdk_rgba_parse(&color, kAccent);
    return color;
}

} // namespace Theme
