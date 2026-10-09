#pragma once

#include "ImageUtils.h"
#include "Library.h"

#include <adwaita.h>
#include <giomm.h>
#include <gtk/gtk.h>
#include <sigc++/sigc++.h>

#include <string>
#include <unordered_map>
#include <vector>

// The library sidebar: one expandable row per album (folder), a search bar,
// and the placeholders shown while there is nothing to list.
class LibraryPanel : public sigc::trackable
{
public:
    enum class Action { PlayNow, PlayNext, Append, Replace };

    LibraryPanel();
    ~LibraryPanel();
    LibraryPanel(const LibraryPanel &) = delete;
    LibraryPanel &operator=(const LibraryPanel &) = delete;

    // The sidebar page (an AdwToolbarView with its own header bar).
    GtkWidget *widget() const { return m_root; }

    void setAlbums(const std::vector<Album> &albums);
    void setThumbnail(const std::string &albumKey, const Image &thumbnail);
    void showScanning(const std::string &root);
    void showEmpty(const std::string &root); // root is "" when none was chosen
    void setStats(const std::string &text, const std::string &tooltip = {});
    void toggleSearch();

    sigc::signal<void(const std::vector<Track> &, Action)> signalTracksRequested;
    sigc::signal<void()> signalChooseFolder;

private:
    GtkWidget *buildAlbumRow(int index);
    void populateTracks(GtkWidget *albumRow, int index);
    void applyFilter();
    bool rowMatches(GtkListBoxRow *row) const;
    std::vector<Track> tracksOf(GtkWidget *row) const;
    void showMenu(GtkWidget *row, double x, double y);
    void request(const std::vector<Track> &tracks, Action action);
    void attachDragSource(GtkWidget *row);
    void attachContextMenu(GtkWidget *row);

    GtkWidget *m_root = nullptr;
    GtkWidget *m_title = nullptr;
    GtkWidget *m_searchBar = nullptr;
    GtkWidget *m_searchEntry = nullptr;
    GtkWidget *m_stack = nullptr;
    GtkWidget *m_list = nullptr;
    GtkWidget *m_emptyPage = nullptr;
    GtkWidget *m_scanningPage = nullptr;
    GtkWidget *m_stats = nullptr;

    std::vector<Album> m_albums;
    std::vector<std::string> m_haystacks; // case-folded search text per album
    std::unordered_map<std::string, GtkWidget *> m_thumbs;  // album key -> GtkImage
    std::unordered_map<std::string, Image> m_thumbCache;    // survives list rebuilds
    std::string m_filter;
    bool m_scanning = false;

    Glib::RefPtr<Gio::SimpleActionGroup> m_actions;
    std::vector<Track> m_menuTracks;
};
