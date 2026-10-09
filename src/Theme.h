#pragma once

#include <gdk/gdk.h>

// Adwaita with Amber's yellowish-orange accent (#e1a34f).
namespace Theme {

// Installs the accent and the few extra style rules, following light/dark.
void install();
// The accent as drawn on dark surfaces (the lyrics backdrop).
GdkRGBA accentOnDark();

} // namespace Theme
