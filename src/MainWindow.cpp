#include "MainWindow.h"

#include "Library.h"
#include "MetadataReader.h"
#include "Mpris.h"
#include "NowPlayingPanel.h"
#include "PlayQueue.h"
#include "Player.h"
#include "QueuePanel.h"
#include "Session.h"

#include "config.h"

namespace {

std::string countText(size_t n, const char *one, const char *many)
{
    return n == 1 ? std::string("1 ") + one : std::to_string(n) + " " + many;
}

std::string absolute(const std::string &path)
{
    gchar *canon = g_canonicalize_filename(path.c_str(), nullptr);
    std::string out(canon);
    g_free(canon);
    return out;
}

} // namespace

MainWindow::MainWindow(Gtk::Application &app)
    : m_app(app)
    , m_queue(std::make_unique<PlayQueue>())
    , m_library(std::make_unique<Library>())
    , m_player(std::make_unique<Player>(*m_queue))
    , m_libraryPanel(std::make_unique<LibraryPanel>())
{
    buildUi();
    buildActions();
    connectSignals();
    restoreSession();

    m_mpris = std::make_unique<Mpris>(*m_player, *m_queue,
                                      Mpris::Hooks{[this] { present(); },
                                                   [this] { gtk_window_close(window()); },
                                                   [this](const std::string &path) { openPaths({path}); }});
    updateTitle();
}

MainWindow::~MainWindow()
{
    teardown();
}

void MainWindow::present()
{
    gtk_window_present(window());
}

// --- construction ------------------------------------------------------------

void MainWindow::buildUi()
{
    m_window = adw_application_window_new(GTK_APPLICATION(m_app.gobj()));
    gtk_window_set_title(window(), "Amber");
    gtk_window_set_icon_name(window(), AMBER_APP_ID);
    gtk_widget_set_size_request(m_window, 360, 400);

    // Content header: sidebar toggle, title, main menu.
    GtkWidget *header = adw_header_bar_new();
    m_windowTitle = adw_window_title_new("Amber", "");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), m_windowTitle);

    GtkWidget *sidebarToggle = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(sidebarToggle), "sidebar-show-symbolic");
    gtk_widget_set_tooltip_text(sidebarToggle, "Show Library (F9)");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), sidebarToggle);

    GMenu *menu = g_menu_new();
    GMenu *files = g_menu_new();
    g_menu_append(files, "Open Music Folder…", "win.open-folder");
    g_menu_append(files, "Add Files to Queue…", "win.add-files");
    g_menu_append(files, "Rescan Library", "win.rescan");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(files));
    GMenu *playback = g_menu_new();
    g_menu_append(playback, "Shuffle", "win.shuffle");
    g_menu_append(playback, "Show Lyrics", "win.lyrics");
    g_menu_append(playback, "Clear Queue", "win.clear-queue");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(playback));
    GMenu *about = g_menu_new();
    g_menu_append(about, "Keyboard Shortcuts", "win.shortcuts");
    g_menu_append(about, "About Amber", "win.about");
    g_menu_append(about, "Quit", "app.quit");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(about));
    GtkWidget *menuButton = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menuButton), "open-menu-symbolic");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), G_MENU_MODEL(menu));
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(menuButton), TRUE);
    gtk_widget_set_tooltip_text(menuButton, "Main Menu");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menuButton);
    g_object_unref(files);
    g_object_unref(playback);
    g_object_unref(about);
    g_object_unref(menu);

    // Queue | now playing.
    m_queuePanel = Gtk::make_managed<QueuePanel>(*m_queue);
    m_nowPlaying = Gtk::make_managed<NowPlayingPanel>();
    m_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(m_paned), GTK_WIDGET(m_queuePanel->gobj()));
    gtk_paned_set_end_child(GTK_PANED(m_paned), GTK_WIDGET(m_nowPlaying->gobj()));
    gtk_paned_set_resize_start_child(GTK_PANED(m_paned), FALSE);
    gtk_paned_set_shrink_start_child(GTK_PANED(m_paned), FALSE);
    gtk_paned_set_shrink_end_child(GTK_PANED(m_paned), FALSE);
    gtk_paned_set_position(GTK_PANED(m_paned), 380);

    GtkWidget *content = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content), m_paned);

    // Library sidebar.
    m_split = adw_overlay_split_view_new();
    adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(m_split), m_libraryPanel->widget());
    adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(m_split), content);
    adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(m_split), 280);
    adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(m_split), 420);
    adw_overlay_split_view_set_sidebar_width_fraction(ADW_OVERLAY_SPLIT_VIEW(m_split), 0.3);
    g_object_bind_property(m_split, "show-sidebar", sidebarToggle, "active",
                           GBindingFlags(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));

    m_toasts = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(m_toasts), m_split);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(m_window), m_toasts);

    // On narrow windows the library slides over the content instead.
    AdwBreakpoint *breakpoint = adw_breakpoint_new(adw_breakpoint_condition_parse("max-width: 960sp"));
    GValue collapsed = G_VALUE_INIT;
    g_value_init(&collapsed, G_TYPE_BOOLEAN);
    g_value_set_boolean(&collapsed, TRUE);
    adw_breakpoint_add_setter(breakpoint, G_OBJECT(m_split), "collapsed", &collapsed);
    g_value_unset(&collapsed);
    adw_application_window_add_breakpoint(ADW_APPLICATION_WINDOW(m_window), breakpoint);

    g_signal_connect(m_window, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer self) -> gboolean {
                         auto *win = static_cast<MainWindow *>(self);
                         win->saveSession();
                         win->teardown();
                         return FALSE;
                     }),
                     this);
}

void MainWindow::buildActions()
{
    m_actions = Gio::SimpleActionGroup::create();
    auto add = [this](const char *name, std::function<void()> fn) {
        m_actions->add_action(name, [this, fn] {
            if (m_player)
                fn();
        });
    };

    add("play-pause", [this] { m_player->togglePlayPause(); });
    add("next", [this] { m_player->next(); });
    add("previous", [this] { m_player->previous(); });
    add("stop", [this] { m_player->stop(); });
    add("seek-forward", [this] { m_player->seek(m_player->position() + 10000); });
    add("seek-backward", [this] { m_player->seek(std::max<int64_t>(0, m_player->position() - 10000)); });
    add("volume-up", [this] { m_player->setVolume(m_player->volume() + 5); });
    add("volume-down", [this] { m_player->setVolume(m_player->volume() - 5); });
    add("repeat", [this] { cycleRepeat(); });
    add("search", [this] {
        adw_overlay_split_view_set_show_sidebar(ADW_OVERLAY_SPLIT_VIEW(m_split), TRUE);
        m_libraryPanel->toggleSearch();
    });
    add("toggle-sidebar", [this] {
        auto *split = ADW_OVERLAY_SPLIT_VIEW(m_split);
        adw_overlay_split_view_set_show_sidebar(split, !adw_overlay_split_view_get_show_sidebar(split));
    });
    add("open-folder", [this] { chooseLibraryFolder(); });
    add("add-files", [this] { addFilesToQueue(); });
    add("rescan", [this] {
        if (!m_library->rootPath().empty() && !m_library->isScanning())
            m_library->rescan();
    });
    add("clear-queue", [this] { m_queue->clear(); });
    add("about", [this] { showAbout(); });
    add("shortcuts", [this] {
        AdwDialog *dialog = adw_alert_dialog_new("Keyboard Shortcuts", nullptr);
        adw_alert_dialog_set_body_use_markup(ADW_ALERT_DIALOG(dialog), TRUE);
        adw_alert_dialog_set_body(ADW_ALERT_DIALOG(dialog),
                                  "<b>Space</b>   Play / pause\n"
                                  "<b>Ctrl+← / →</b>   Previous / next track\n"
                                  "<b>Shift+← / →</b>   Seek 10 s\n"
                                  "<b>Ctrl+↑ / ↓</b>   Volume up / down\n"
                                  "<b>M</b>   Mute\n"
                                  "<b>Ctrl+S</b>   Shuffle\n"
                                  "<b>Ctrl+R</b>   Repeat: off → queue → track\n"
                                  "<b>L</b>   Show / hide lyrics\n"
                                  "<b>Ctrl+F</b>   Search the library\n"
                                  "<b>F9</b>   Show / hide the library\n"
                                  "<b>F5</b>   Rescan the library\n"
                                  "<b>Delete</b>   Remove the selected queue entries");
        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "close", "Close");
        adw_dialog_present(dialog, m_window);
    });

    m_shuffleAction = m_actions->add_action_bool("shuffle", [this] {
        if (m_queue)
            m_queue->setShuffle(!m_queue->shuffle());
    });
    m_lyricsAction = m_actions->add_action_bool("lyrics", [this] {
        if (m_nowPlaying)
            m_nowPlaying->setLyricsEnabled(!m_nowPlaying->lyricsEnabled());
        m_lyricsAction->set_state(Glib::Variant<bool>::create(m_nowPlaying->lyricsEnabled()));
    }, true);
    m_muteAction = m_actions->add_action_bool("mute", [this] {
        if (m_player)
            m_player->setMuted(!m_player->isMuted());
    });

    gtk_widget_insert_action_group(m_window, "win", G_ACTION_GROUP(m_actions->gobj()));

    const std::pair<const char *, std::vector<Glib::ustring>> accels[] = {
        {"win.next", {"<Control>Right"}},
        {"win.previous", {"<Control>Left"}},
        {"win.stop", {"<Control>period"}},
        {"win.seek-forward", {"<Shift>Right"}},
        {"win.seek-backward", {"<Shift>Left"}},
        {"win.volume-up", {"<Control>Up"}},
        {"win.volume-down", {"<Control>Down"}},
        {"win.shuffle", {"<Control>s"}},
        {"win.repeat", {"<Control>r"}},
        {"win.search", {"<Control>f"}},
        {"win.toggle-sidebar", {"F9"}},
        {"win.open-folder", {"<Control>o"}},
        {"win.add-files", {"<Control><Shift>o"}},
        {"win.rescan", {"F5"}},
        {"app.quit", {"<Control>q"}},
    };
    for (const auto &[action, keys] : accels)
        m_app.set_accels_for_action(action, keys);

    // Single-key shortcuts, unless a text field has the keyboard.
    auto controller = Gtk::ShortcutController::create();
    controller->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    const std::pair<const char *, const char *> keys[] = {
        {"space", "play-pause"}, {"m", "mute"}, {"l", "lyrics"},
        {"AudioPlay", "play-pause"}, {"AudioPause", "play-pause"}, {"AudioNext", "next"},
        {"AudioPrev", "previous"}, {"AudioStop", "stop"},
    };
    for (const auto &[trigger, action] : keys) {
        const std::string name = action;
        controller->add_shortcut(Gtk::Shortcut::create(
            Gtk::ShortcutTrigger::parse_string(trigger),
            Gtk::CallbackAction::create([this, name](Gtk::Widget &, const Glib::VariantBase &) {
                if (!shortcutAllowed())
                    return false;
                m_actions->activate_action(name);
                return true;
            })));
    }
    gtk_widget_add_controller(m_window, GTK_EVENT_CONTROLLER(controller->gobj_copy()));
}

bool MainWindow::shortcutAllowed() const
{
    GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(m_window));
    return !(focus && GTK_IS_EDITABLE(focus));
}

void MainWindow::connectSignals()
{
    NowPlayingPanel *np = m_nowPlaying;
    np->signalPlayPause.connect([this] { m_actions->activate_action("play-pause"); });
    np->signalNext.connect([this] { m_actions->activate_action("next"); });
    np->signalPrevious.connect([this] { m_actions->activate_action("previous"); });
    np->signalSeek.connect([this](int64_t ms) {
        if (m_player)
            m_player->seek(ms);
    });
    np->signalVolume.connect([this](int v) {
        if (m_player)
            m_player->setVolume(v);
    });
    np->signalMuteToggled.connect([this](bool muted) {
        if (m_player)
            m_player->setMuted(muted);
    });
    np->signalShuffleToggled.connect([this](bool on) { m_queue->setShuffle(on); });
    np->signalRepeatClicked.connect([this] { cycleRepeat(); });
    np->signalLyricsToggled.connect([this](bool on) { m_lyricsAction->set_state(Glib::Variant<bool>::create(on)); });

    m_player->signalTrackChanged.connect([this, np](const Track &t) {
        np->setTrack(t);
        updateTitle();
    });
    m_player->signalCoverChanged.connect([np](const Image &cover) { np->setCover(cover); });
    m_player->signalLyricsChanged.connect([np](const Lyrics &lyrics) { np->setLyrics(lyrics); });
    m_player->signalPositionChanged.connect([np](int64_t ms) { np->setPosition(ms); });
    m_player->signalDurationChanged.connect([np](int64_t ms) { np->setDuration(ms); });
    m_player->signalVolumeChanged.connect([np](int v) { np->setVolume(v); });
    m_player->signalMutedChanged.connect([this, np](bool muted) {
        np->setMuted(muted);
        m_muteAction->set_state(Glib::Variant<bool>::create(muted));
    });
    m_player->signalStatusChanged.connect([this, np](Player::Status status) {
        np->setPlaying(status == Player::Status::Playing);
        m_queuePanel->setPlaying(status == Player::Status::Playing);
        updateTitle();
    });
    m_player->signalError.connect([this](const std::string &message) { toast(message); });

    m_queue->signalShuffleChanged.connect([this, np](bool on) {
        np->setShuffle(on);
        m_shuffleAction->set_state(Glib::Variant<bool>::create(on));
    });
    m_queue->signalRepeatChanged.connect([np](PlayQueue::Repeat mode) { np->setRepeat(mode); });

    m_queuePanel->signalRowActivated.connect([this](int row) {
        if (m_player)
            m_player->playRow(row);
    });
    m_queuePanel->signalPathsDropped.connect([this](const std::vector<std::string> &paths, int row) {
        enqueue(tracksFromPaths(paths), LibraryPanel::Action::Append, row);
    });

    m_libraryPanel->signalTracksRequested.connect(
        [this](const std::vector<Track> &tracks, LibraryPanel::Action action) { enqueue(tracks, action); });
    m_libraryPanel->signalChooseFolder.connect([this] { chooseLibraryFolder(); });

    m_library->signalScanStarted.connect([this] {
        m_libraryPanel->showScanning(m_library->rootPath());
        m_libraryPanel->setStats("Scanning…", m_library->rootPath());
    });
    m_library->signalScanProgress.connect([this](int files) {
        m_libraryPanel->setStats("Scanning… " + std::to_string(files) + " files", m_library->rootPath());
    });
    m_library->signalScanFinished.connect([this] { onScanFinished(); });
    m_library->signalThumbnailReady.connect([this](const std::string &key, const Image &thumb) {
        m_libraryPanel->setThumbnail(key, thumb);
    });
}

// --- behaviour ------------------------------------------------------------------

void MainWindow::cycleRepeat()
{
    switch (m_queue->repeat()) {
    case PlayQueue::Repeat::Off:
        m_queue->setRepeat(PlayQueue::Repeat::All);
        break;
    case PlayQueue::Repeat::All:
        m_queue->setRepeat(PlayQueue::Repeat::One);
        break;
    case PlayQueue::Repeat::One:
        m_queue->setRepeat(PlayQueue::Repeat::Off);
        break;
    }
}

void MainWindow::toast(const std::string &message)
{
    if (!m_toasts)
        return;
    gchar *escaped = g_markup_escape_text(message.c_str(), -1);
    AdwToast *toast = adw_toast_new(escaped);
    g_free(escaped);
    adw_toast_set_timeout(toast, 4);
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(m_toasts), toast);
}

void MainWindow::chooseLibraryFolder()
{
    std::string start = m_library->rootPath();
    if (start.empty())
        if (const char *music = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC))
            start = music;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Choose Music Folder");
    if (!start.empty()) {
        GFile *folder = g_file_new_for_path(start.c_str());
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
    }
    gtk_file_dialog_select_folder(dialog, window(), nullptr, +[](GObject *source, GAsyncResult *result, gpointer self) {
        GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, nullptr);
        auto *win = static_cast<MainWindow *>(self);
        if (file && win->m_library) {
            if (gchar *path = g_file_get_path(file)) {
                win->m_library->scan(path);
                g_free(path);
            }
        }
        if (file)
            g_object_unref(file);
    }, this);
    g_object_unref(dialog);
}

void MainWindow::addFilesToQueue()
{
    GtkFileFilter *audio = gtk_file_filter_new();
    gtk_file_filter_set_name(audio, "Audio files (MP3, FLAC, WebM)");
    for (const std::string &suffix : Formats::suffixes())
        gtk_file_filter_add_suffix(audio, suffix.c_str());
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, audio);

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Add Files to Queue");
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_set_default_filter(dialog, audio);
    std::string start = m_library->rootPath();
    if (start.empty())
        if (const char *music = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC))
            start = music;
    if (!start.empty()) {
        GFile *folder = g_file_new_for_path(start.c_str());
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
    }
    g_object_unref(filters);
    g_object_unref(audio);

    gtk_file_dialog_open_multiple(dialog, window(), nullptr, +[](GObject *source, GAsyncResult *result, gpointer self) {
        GListModel *files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, nullptr);
        if (!files)
            return;
        std::vector<std::string> paths;
        for (guint i = 0; i < g_list_model_get_n_items(files); ++i) {
            GFile *file = G_FILE(g_list_model_get_item(files, i));
            if (gchar *path = g_file_get_path(file)) {
                paths.emplace_back(path);
                g_free(path);
            }
            g_object_unref(file);
        }
        g_object_unref(files);
        auto *win = static_cast<MainWindow *>(self);
        win->enqueue(win->tracksFromPaths(paths), LibraryPanel::Action::Append);
    }, this);
    g_object_unref(dialog);
}

void MainWindow::openPaths(const std::vector<std::string> &paths)
{
    std::vector<std::string> absolutePaths;
    for (const std::string &p : paths)
        absolutePaths.push_back(absolute(p));
    const std::vector<Track> tracks = tracksFromPaths(absolutePaths);
    if (!tracks.empty())
        enqueue(tracks, LibraryPanel::Action::PlayNow);
}

std::vector<Track> MainWindow::tracksFromPaths(const std::vector<std::string> &paths) const
{
    std::vector<Track> tracks;
    if (!m_library)
        return tracks;
    for (const std::string &path : paths) {
        if (Paths::isDir(path)) {
            for (const std::string &file : Library::collectFiles(path))
                tracks.push_back(m_library->track(file));
        } else if (Paths::isFile(path) && Formats::isSupported(path)) {
            tracks.push_back(m_library->track(path));
        }
    }
    return tracks;
}

void MainWindow::enqueue(const std::vector<Track> &tracks, LibraryPanel::Action action, int row)
{
    if (tracks.empty() || !m_player)
        return;

    // Starting from silence, queueing something should simply start it.
    const bool idle = !m_player->hasTrack() && !m_player->isPlaying();
    const std::string what = countText(tracks.size(), "track", "tracks");

    switch (action) {
    case LibraryPanel::Action::Append: {
        const int first = row >= 0 ? m_queue->insert(row, tracks) : m_queue->append(tracks);
        toast("Added " + what + " to the queue");
        if (idle)
            m_player->playRow(m_queue->shuffle() ? m_queue->advance(false) : first);
        break;
    }
    case LibraryPanel::Action::PlayNext:
        m_queue->insertAfterCurrent(tracks);
        toast(what + " will play next");
        if (idle)
            m_player->next();
        break;
    case LibraryPanel::Action::PlayNow:
        m_player->playRow(m_queue->insertAfterCurrent(tracks));
        break;
    case LibraryPanel::Action::Replace:
        m_queue->clear();
        m_queue->append(tracks);
        m_player->next();
        break;
    }
}

void MainWindow::onScanFinished()
{
    const auto &albums = m_library->albums();
    m_libraryPanel->setAlbums(albums);
    if (albums.empty()) {
        m_libraryPanel->showEmpty(m_library->rootPath());
        m_libraryPanel->setStats("", m_library->rootPath());
        return;
    }
    m_libraryPanel->setStats(countText(albums.size(), "album", "albums") + " · "
                                 + countText(size_t(m_library->trackCount()), "track", "tracks"),
                             m_library->rootPath());
}

void MainWindow::updateTitle()
{
    if (!m_player || !m_player->hasTrack()) {
        gtk_window_set_title(window(), "Amber");
        adw_window_title_set_title(ADW_WINDOW_TITLE(m_windowTitle), "Amber");
        adw_window_title_set_subtitle(ADW_WINDOW_TITLE(m_windowTitle), "");
        return;
    }
    const Track &t = m_player->currentTrack();
    const std::string title = t.displayTitle() + " — " + t.displayArtist();
    gtk_window_set_title(window(), ((m_player->isPlaying() ? "" : "(paused) ") + title + " · Amber").c_str());
    adw_window_title_set_title(ADW_WINDOW_TITLE(m_windowTitle), t.displayTitle().c_str());
    adw_window_title_set_subtitle(ADW_WINDOW_TITLE(m_windowTitle),
                                  (t.displayArtist() + (m_player->isPlaying() ? "" : " · paused")).c_str());
}

void MainWindow::showAbout()
{
    AdwDialog *about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "Amber");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), AMBER_APP_ID);
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), AMBER_VERSION);
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "knap43");
    adw_about_dialog_set_comments(
        ADW_ABOUT_DIALOG(about),
        "A small player for local MP3, FLAC and WebM files.\n\n"
        "Folders are albums. Lyrics come from a matching .lrc file or from the tags; "
        "click a synced line to jump to it. Controllable through MPRIS (playerctl -p amber).");
    adw_dialog_present(about, m_window);
}

// --- session ----------------------------------------------------------------------

void MainWindow::restoreSession()
{
    const Session s = Session::load();
    gtk_window_set_default_size(window(), s.width, s.height);
    if (s.maximized)
        gtk_window_maximize(window());
    if (s.panedPosition > 0)
        gtk_paned_set_position(GTK_PANED(m_paned), s.panedPosition);
    adw_overlay_split_view_set_show_sidebar(ADW_OVERLAY_SPLIT_VIEW(m_split), s.showSidebar);

    m_player->setVolume(s.volume);
    m_player->setMuted(s.muted);
    m_nowPlaying->setVolume(m_player->volume());
    m_nowPlaying->setMuted(m_player->isMuted());
    m_muteAction->set_state(Glib::Variant<bool>::create(m_player->isMuted()));
    m_nowPlaying->setLyricsEnabled(s.lyrics);
    m_lyricsAction->set_state(Glib::Variant<bool>::create(s.lyrics));

    std::vector<Track> tracks;
    for (const std::string &path : s.queuePaths)
        if (Paths::isFile(path))
            tracks.push_back(Metadata::readTrack(path));
    m_queue->append(tracks);
    m_queue->setShuffle(s.shuffle);
    m_queue->setRepeat(PlayQueue::Repeat(std::clamp(s.repeat, 0, 2)));
    if (s.queueCurrent >= 0 && s.queueCurrent < m_queue->count()) {
        m_queue->setCurrentRow(s.queueCurrent);
        m_player->restoreCurrent(s.queuePosition);
    }

    std::string root = s.libraryRoot;
    if (root.empty()) {
        const char *music = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
        if (music && Paths::isDir(music) && std::string(music) != g_get_home_dir())
            root = music;
    }
    if (!root.empty() && Paths::isDir(root))
        m_library->scan(root);
    else
        m_libraryPanel->showEmpty(root.empty() ? std::string() : root);
}

void MainWindow::saveSession()
{
    if (!m_player)
        return;
    Session s;
    gtk_window_get_default_size(window(), &s.width, &s.height);
    s.maximized = gtk_window_is_maximized(window());
    s.panedPosition = gtk_paned_get_position(GTK_PANED(m_paned));
    s.showSidebar = adw_overlay_split_view_get_show_sidebar(ADW_OVERLAY_SPLIT_VIEW(m_split));
    s.libraryRoot = m_library->rootPath();
    s.volume = m_player->volume();
    s.muted = m_player->isMuted();
    s.shuffle = m_queue->shuffle();
    s.repeat = int(m_queue->repeat());
    s.lyrics = m_nowPlaying->lyricsEnabled();
    for (const Track &t : m_queue->tracks())
        s.queuePaths.push_back(t.path);
    s.queueCurrent = m_queue->currentRow();
    s.queuePosition = m_player->position();
    s.save();
}

void MainWindow::teardown()
{
    // Stop everything that could still call into the widgets, which are about
    // to be destroyed together with the window.
    m_mpris.reset();
    m_player.reset();
    m_library.reset();
}
