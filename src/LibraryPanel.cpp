#include "LibraryPanel.h"

#include "CoverLyricsView.h"
#include "DragData.h"

namespace {

const char *const kAlbumKey = "amber-album";  // index + 1
const char *const kTrackKey = "amber-track";  // index + 1
const char *const kPanelKey = "amber-panel";

int indexOf(GtkWidget *row, const char *key)
{
    return GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), key)) - 1;
}

LibraryPanel *panelOf(GtkWidget *row)
{
    return static_cast<LibraryPanel *>(g_object_get_data(G_OBJECT(row), kPanelKey));
}

std::string trackCountText(size_t n)
{
    return n == 1 ? "1 track" : std::to_string(n) + " tracks";
}

GtkWidget *flatIconButton(const char *icon, const char *tooltip)
{
    GtkWidget *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "circular");
    return button;
}

} // namespace

LibraryPanel::LibraryPanel()
{
    // Header bar: title, open-folder and search.
    GtkWidget *header = adw_header_bar_new();
    m_title = adw_window_title_new("Library", "");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), m_title);
    GtkWidget *open = gtk_button_new_from_icon_name("folder-open-symbolic");
    gtk_widget_set_tooltip_text(open, "Open Music Folder… (Ctrl+O)");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open), "win.open-folder");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), open);
    GtkWidget *searchToggle = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(searchToggle), "system-search-symbolic");
    gtk_widget_set_tooltip_text(searchToggle, "Search (Ctrl+F)");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), searchToggle);

    // Search.
    m_searchEntry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(m_searchEntry), "Search albums, artists, tracks");
    gtk_widget_set_hexpand(m_searchEntry, TRUE);
    m_searchBar = gtk_search_bar_new();
    gtk_search_bar_set_child(GTK_SEARCH_BAR(m_searchBar), m_searchEntry);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(m_searchBar), GTK_EDITABLE(m_searchEntry));
    g_object_bind_property(searchToggle, "active", m_searchBar, "search-mode-enabled",
                           GBindingFlags(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));
    g_signal_connect(m_searchEntry, "search-changed", G_CALLBACK(+[](GtkSearchEntry *entry, gpointer self) {
                         auto *panel = static_cast<LibraryPanel *>(self);
                         panel->m_filter = Text::casefold(Text::trimmed(gtk_editable_get_text(GTK_EDITABLE(entry))));
                         panel->applyFilter();
                     }),
                     this);
    g_signal_connect(m_searchBar, "notify::search-mode-enabled", G_CALLBACK(+[](GObject *bar, GParamSpec *, gpointer self) {
                         auto *panel = static_cast<LibraryPanel *>(self);
                         if (!gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(bar)))
                             gtk_editable_set_text(GTK_EDITABLE(panel->m_searchEntry), "");
                     }),
                     this);

    // The album list.
    m_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(m_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(m_list, "boxed-list");
    gtk_widget_set_valign(m_list, GTK_ALIGN_START);
    gtk_widget_set_margin_start(m_list, 12);
    gtk_widget_set_margin_end(m_list, 12);
    gtk_widget_set_margin_top(m_list, 6);
    gtk_widget_set_margin_bottom(m_list, 12);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(m_list), +[](GtkListBoxRow *row, gpointer self) -> gboolean {
        return static_cast<LibraryPanel *>(self)->rowMatches(row);
    }, this, nullptr);
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), m_list);
    gtk_widget_set_vexpand(scroller, TRUE);

    // Placeholders.
    m_emptyPage = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(m_emptyPage), "folder-music-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(m_emptyPage), "No Music Folder");
    GtkWidget *choose = gtk_button_new_with_label("Choose Music Folder…");
    gtk_widget_set_halign(choose, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(choose, "pill");
    gtk_widget_add_css_class(choose, "suggested-action");
    g_signal_connect(choose, "clicked", G_CALLBACK(+[](GtkButton *, gpointer self) {
                         static_cast<LibraryPanel *>(self)->signalChooseFolder.emit();
                     }),
                     this);
    adw_status_page_set_child(ADW_STATUS_PAGE(m_emptyPage), choose);
    gtk_widget_add_css_class(m_emptyPage, "compact");

    m_scanningPage = adw_status_page_new();
    adw_status_page_set_title(ADW_STATUS_PAGE(m_scanningPage), "Scanning…");
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_set_spinning(GTK_SPINNER(spinner), TRUE);
    gtk_widget_set_size_request(spinner, 32, 32);
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    adw_status_page_set_child(ADW_STATUS_PAGE(m_scanningPage), spinner);
    gtk_widget_add_css_class(m_scanningPage, "compact");

    GtkWidget *noResults = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(noResults), "edit-find-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(noResults), "No Results");
    adw_status_page_set_description(ADW_STATUS_PAGE(noResults), "Try a different search");
    gtk_widget_add_css_class(noResults, "compact");

    m_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(m_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_add_named(GTK_STACK(m_stack), m_emptyPage, "empty");
    gtk_stack_add_named(GTK_STACK(m_stack), m_scanningPage, "scanning");
    gtk_stack_add_named(GTK_STACK(m_stack), scroller, "list");
    gtk_stack_add_named(GTK_STACK(m_stack), noResults, "no-results");

    // Library statistics at the bottom.
    m_stats = gtk_label_new("");
    gtk_widget_add_css_class(m_stats, "dim-label");
    gtk_widget_add_css_class(m_stats, "caption");
    gtk_label_set_ellipsize(GTK_LABEL(m_stats), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_margin_top(m_stats, 6);
    gtk_widget_set_margin_bottom(m_stats, 8);

    m_root = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(m_root), header);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(m_root), m_searchBar);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(m_root), m_stack);
    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(m_root), m_stats);
    g_object_ref_sink(m_root);

    // Context-menu actions.
    m_actions = Gio::SimpleActionGroup::create();
    const std::pair<const char *, Action> actions[] = {{"play-now", Action::PlayNow},
                                                        {"play-next", Action::PlayNext},
                                                        {"add", Action::Append},
                                                        {"replace", Action::Replace}};
    for (const auto &[name, action] : actions) {
        m_actions->add_action(name, [this, action = action] { request(m_menuTracks, action); });
    }
    gtk_widget_insert_action_group(m_root, "lib", G_ACTION_GROUP(m_actions->gobj()));

    showEmpty({});
}

LibraryPanel::~LibraryPanel()
{
    g_object_unref(m_root);
}

void LibraryPanel::request(const std::vector<Track> &tracks, Action action)
{
    if (!tracks.empty())
        signalTracksRequested.emit(tracks, action);
}

void LibraryPanel::showEmpty(const std::string &root)
{
    m_scanning = false;
    if (root.empty()) {
        adw_status_page_set_title(ADW_STATUS_PAGE(m_emptyPage), "No Music Folder");
        adw_status_page_set_description(ADW_STATUS_PAGE(m_emptyPage),
                                        "Pick the folder that holds your music. MP3, FLAC, Ogg, Opus and WebM files are supported.");
    } else {
        adw_status_page_set_title(ADW_STATUS_PAGE(m_emptyPage), "No Music Found");
        gchar *escaped = g_markup_escape_text(root.c_str(), -1);
        const std::string text = std::string("No MP3, FLAC, Ogg, Opus or WebM files were found in ") + escaped;
        g_free(escaped);
        adw_status_page_set_description(ADW_STATUS_PAGE(m_emptyPage), text.c_str());
    }
    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "empty");
}

void LibraryPanel::showScanning(const std::string &root)
{
    m_scanning = true;
    if (m_albums.empty()) {
        gchar *escaped = g_markup_escape_text(root.c_str(), -1);
        adw_status_page_set_description(ADW_STATUS_PAGE(m_scanningPage), escaped);
        g_free(escaped);
        gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "scanning");
    }
}

void LibraryPanel::setStats(const std::string &text, const std::string &tooltip)
{
    gtk_label_set_text(GTK_LABEL(m_stats), text.c_str());
    gtk_widget_set_tooltip_text(m_stats, tooltip.empty() ? nullptr : tooltip.c_str());
    adw_window_title_set_subtitle(ADW_WINDOW_TITLE(m_title), text.c_str());
}

void LibraryPanel::toggleSearch()
{
    const bool on = !gtk_search_bar_get_search_mode(GTK_SEARCH_BAR(m_searchBar));
    gtk_search_bar_set_search_mode(GTK_SEARCH_BAR(m_searchBar), on);
    if (on)
        gtk_widget_grab_focus(m_searchEntry);
}

void LibraryPanel::setAlbums(const std::vector<Album> &albums)
{
    m_scanning = false;
    m_albums = albums;
    m_thumbs.clear();
    m_haystacks.clear();
    m_haystacks.reserve(m_albums.size());
    for (const Album &album : m_albums) {
        std::string text = album.title + "\n" + album.location;
        for (const Track &t : album.tracks)
            text += "\n" + t.displayTitle() + "\n" + t.artist;
        m_haystacks.push_back(Text::casefold(text));
    }

    gtk_list_box_remove_all(GTK_LIST_BOX(m_list));
    for (int i = 0; i < int(m_albums.size()); ++i)
        gtk_list_box_append(GTK_LIST_BOX(m_list), buildAlbumRow(i));
    applyFilter();
}

GtkWidget *LibraryPanel::buildAlbumRow(int index)
{
    const Album &album = m_albums[size_t(index)];
    GtkWidget *row = adw_expander_row_new();
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), album.title.c_str());
    const std::string subtitle = album.location + "  ·  " + trackCountText(album.tracks.size()) + ", "
                                 + Format::duration(album.durationMs());
    adw_expander_row_set_subtitle(ADW_EXPANDER_ROW(row), subtitle.c_str());
    adw_expander_row_set_subtitle_lines(ADW_EXPANDER_ROW(row), 1);
    adw_expander_row_set_title_lines(ADW_EXPANDER_ROW(row), 1);
    g_object_set_data(G_OBJECT(row), kAlbumKey, GINT_TO_POINTER(index + 1));
    g_object_set_data(G_OBJECT(row), kPanelKey, this);

    GtkWidget *thumb = gtk_image_new_from_icon_name("folder-music-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(thumb), 40);
    gtk_widget_set_size_request(thumb, 40, 40);
    gtk_widget_set_overflow(thumb, GTK_OVERFLOW_HIDDEN);
    gtk_widget_add_css_class(thumb, "library-row-thumb");
    adw_expander_row_add_prefix(ADW_EXPANDER_ROW(row), thumb);
    m_thumbs[album.key] = thumb;
    const auto cached = m_thumbCache.find(album.key);
    if (cached != m_thumbCache.end())
        setThumbnail(album.key, cached->second);

    GtkWidget *add = flatIconButton("list-add-symbolic", "Add to Queue");
    GtkWidget *play = flatIconButton("media-playback-start-symbolic", "Play Now");
    g_object_set_data(G_OBJECT(add), kAlbumKey, GINT_TO_POINTER(index + 1));
    g_object_set_data(G_OBJECT(play), kAlbumKey, GINT_TO_POINTER(index + 1));
    g_signal_connect(add, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer self) {
                         auto *panel = static_cast<LibraryPanel *>(self);
                         panel->request(panel->m_albums[size_t(indexOf(GTK_WIDGET(b), kAlbumKey))].tracks,
                                        Action::Append);
                     }),
                     this);
    g_signal_connect(play, "clicked", G_CALLBACK(+[](GtkButton *b, gpointer self) {
                         auto *panel = static_cast<LibraryPanel *>(self);
                         panel->request(panel->m_albums[size_t(indexOf(GTK_WIDGET(b), kAlbumKey))].tracks,
                                        Action::PlayNow);
                     }),
                     this);
    adw_expander_row_add_suffix(ADW_EXPANDER_ROW(row), add);
    adw_expander_row_add_suffix(ADW_EXPANDER_ROW(row), play);

    // Track rows are created the first time the album is expanded.
    g_signal_connect(row, "notify::expanded", G_CALLBACK(+[](GObject *object, GParamSpec *, gpointer self) {
                         GtkWidget *albumRow = GTK_WIDGET(object);
                         if (adw_expander_row_get_expanded(ADW_EXPANDER_ROW(albumRow))
                             && !g_object_get_data(object, "amber-populated")) {
                             g_object_set_data(object, "amber-populated", GINT_TO_POINTER(1));
                             static_cast<LibraryPanel *>(self)->populateTracks(albumRow, indexOf(albumRow, kAlbumKey));
                         }
                     }),
                     this);

    attachDragSource(row);
    attachContextMenu(row);
    return row;
}

void LibraryPanel::populateTracks(GtkWidget *albumRow, int index)
{
    const Album &album = m_albums[size_t(index)];
    for (int i = 0; i < int(album.tracks.size()); ++i) {
        const Track &t = album.tracks[size_t(i)];
        GtkWidget *row = adw_action_row_new();
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), t.displayTitle().c_str());
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), t.displayArtist().c_str());
        adw_action_row_set_title_lines(ADW_ACTION_ROW(row), 1);
        adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(row), 1);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), TRUE);
        gtk_widget_set_tooltip_text(row, (t.path + "\nClick to add to the queue").c_str());
        g_object_set_data(G_OBJECT(row), kAlbumKey, GINT_TO_POINTER(index + 1));
        g_object_set_data(G_OBJECT(row), kTrackKey, GINT_TO_POINTER(i + 1));
        g_object_set_data(G_OBJECT(row), kPanelKey, this);

        GtkWidget *number = gtk_label_new(std::to_string(t.trackNumber > 0 ? t.trackNumber : i + 1).c_str());
        gtk_widget_add_css_class(number, "dim-label");
        gtk_widget_add_css_class(number, "numeric");
        gtk_label_set_width_chars(GTK_LABEL(number), 2);
        gtk_label_set_xalign(GTK_LABEL(number), 1);
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), number);

        GtkWidget *length = gtk_label_new(Format::duration(t.durationMs).c_str());
        gtk_widget_add_css_class(length, "dim-label");
        gtk_widget_add_css_class(length, "numeric");
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), length);

        g_signal_connect(row, "activated", G_CALLBACK(+[](AdwActionRow *r, gpointer self) {
                             auto *panel = static_cast<LibraryPanel *>(self);
                             panel->request(panel->tracksOf(GTK_WIDGET(r)), Action::Append);
                         }),
                         this);
        attachDragSource(row);
        attachContextMenu(row);
        adw_expander_row_add_row(ADW_EXPANDER_ROW(albumRow), row);
    }
}

std::vector<Track> LibraryPanel::tracksOf(GtkWidget *row) const
{
    const int album = indexOf(row, kAlbumKey);
    if (album < 0 || album >= int(m_albums.size()))
        return {};
    const int track = indexOf(row, kTrackKey);
    const auto &tracks = m_albums[size_t(album)].tracks;
    if (track < 0)
        return tracks;
    if (track >= int(tracks.size()))
        return {};
    return {tracks[size_t(track)]};
}

void LibraryPanel::attachDragSource(GtkWidget *row)
{
    GtkDragSource *source = gtk_drag_source_new();
    gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
    g_signal_connect(source, "prepare", G_CALLBACK(+[](GtkDragSource *src, double, double, gpointer) {
                         GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(src));
                         LibraryPanel *panel = panelOf(widget);
                         std::vector<std::string> paths;
                         for (const Track &t : panel->tracksOf(widget))
                             paths.push_back(t.path);
                         return paths.empty() ? static_cast<GdkContentProvider *>(nullptr) : DragData::forPaths(paths);
                     }),
                     nullptr);
    DragData::useWidgetAsIcon(source, row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(source));
}

void LibraryPanel::attachContextMenu(GtkWidget *row)
{
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_SECONDARY);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, int, double x, double y, gpointer) {
                         GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
                         gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
                         panelOf(widget)->showMenu(widget, x, y);
                     }),
                     nullptr);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(click));
}

void LibraryPanel::showMenu(GtkWidget *row, double x, double y)
{
    m_menuTracks = tracksOf(row);
    if (m_menuTracks.empty())
        return;

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Play Now", "lib.play-now");
    g_menu_append(menu, "Play Next", "lib.play-next");
    g_menu_append(menu, "Add to Queue", "lib.add");
    g_menu_append(menu, "Replace Queue", "lib.replace");
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_widget_set_parent(popover, row);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_halign(popover, GTK_ALIGN_START);
    const GdkRectangle at{int(x), int(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
    g_signal_connect(popover, "closed", G_CALLBACK(+[](GtkPopover *p, gpointer) {
                         // Unparent once the menu's action has run.
                         g_idle_add([](gpointer w) -> gboolean {
                             gtk_widget_unparent(GTK_WIDGET(w));
                             return G_SOURCE_REMOVE;
                         }, p);
                     }),
                     nullptr);
    gtk_popover_popup(GTK_POPOVER(popover));
}

void LibraryPanel::setThumbnail(const std::string &albumKey, const Image &thumbnail)
{
    m_thumbCache[albumKey] = thumbnail;
    const auto it = m_thumbs.find(albumKey);
    if (it == m_thumbs.end())
        return;
    Texture texture = makeTexture(thumbnail);
    if (texture)
        gtk_image_set_from_paintable(GTK_IMAGE(it->second), GDK_PAINTABLE(texture.get()));
}

bool LibraryPanel::rowMatches(GtkListBoxRow *row) const
{
    if (m_filter.empty())
        return true;
    const int index = indexOf(GTK_WIDGET(row), kAlbumKey);
    return index >= 0 && index < int(m_haystacks.size())
           && m_haystacks[size_t(index)].find(m_filter) != std::string::npos;
}

void LibraryPanel::applyFilter()
{
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(m_list));
    if (m_albums.empty()) {
        if (!m_scanning && g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(m_stack)), "list") == 0)
            gtk_stack_set_visible_child_name(GTK_STACK(m_stack), "empty");
        return;
    }
    bool any = m_filter.empty();
    for (size_t i = 0; !any && i < m_haystacks.size(); ++i)
        any = m_haystacks[i].find(m_filter) != std::string::npos;
    gtk_stack_set_visible_child_name(GTK_STACK(m_stack), any ? "list" : "no-results");
}
