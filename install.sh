#!/usr/bin/env bash
# Builds and installs Amber.
#
#   ./install.sh               install system-wide into /usr/local (uses sudo)
#   ./install.sh --user        install for this user only, into ~/.local
#   ./install.sh --uninstall   remove what a previous run installed
#                              (add --user if it was a user install)
#
# On Arch Linux, missing build dependencies are offered for installation.
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

prefix=/usr/local
sudo=sudo
action=install
for arg in "$@"; do
    case "$arg" in
        --user) prefix="$HOME/.local"; sudo= ;;
        --uninstall) action=uninstall ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $arg (see --help)" >&2; exit 1 ;;
    esac
done
[ "$(id -u)" -eq 0 ] && sudo=
builddir="build-install"

refresh_caches() {
    local share="$prefix/share"
    command -v gtk-update-icon-cache >/dev/null &&
        $sudo gtk-update-icon-cache -qtf "$share/icons/hicolor" 2>/dev/null || true
    command -v update-desktop-database >/dev/null &&
        $sudo update-desktop-database -q "$share/applications" 2>/dev/null || true
}

if [ "$action" = uninstall ]; then
    if [ ! -d "$builddir" ]; then
        echo "Nothing to uninstall: $builddir/ (left by the install) is missing." >&2
        exit 1
    fi
    $sudo ninja -C "$builddir" uninstall
    refresh_caches
    echo "Amber has been removed. Your settings and caches are kept in"
    echo "~/.config/amber-player and ~/.cache/amber-player."
    exit 0
fi

# Dependencies.
if command -v pacman >/dev/null; then
    deps=(base-devel meson gtkmm-4.0 libadwaita taglib gstreamer gst-plugins-base gst-plugins-good)
    missing=()
    for p in "${deps[@]}"; do
        pacman -Qq "$p" >/dev/null 2>&1 || pacman -Qqg "$p" >/dev/null 2>&1 || missing+=("$p")
    done
    if [ ${#missing[@]} -gt 0 ]; then
        echo "Missing packages: ${missing[*]}"
        sudo pacman -S --needed "${missing[@]}"
    fi
else
    for tool in meson ninja pkg-config; do
        command -v "$tool" >/dev/null || { echo "Please install $tool first." >&2; exit 1; }
    done
fi

# Build, test, install.
if [ -d "$builddir" ]; then
    meson configure "$builddir" --prefix="$prefix" --buildtype=release >/dev/null
else
    meson setup "$builddir" --prefix="$prefix" --buildtype=release
fi
meson compile -C "$builddir"
meson test -C "$builddir" --print-errorlogs
$sudo meson install -C "$builddir" --no-rebuild
refresh_caches

echo
echo "Amber is installed in $prefix. Start it from your app menu, or run: amber-player"
if [ "$prefix" = "$HOME/.local" ] && [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
    echo "Note: ~/.local/bin is not on your PATH, so add it to run amber-player from a terminal."
fi
