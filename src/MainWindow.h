#pragma once

#include "LibraryPanel.h"

#include <adwaita.h>
#include <giomm.h>
#include <gtkmm.h>

#include <memory>
#include <string>
#include <vector>

class Library;
class Mpris;
class NowPlayingPanel;
class PlayQueue;
class Player;
class QueuePanel;

// The application window: library sidebar | queue | now playing, plus the
// actions, shortcuts, session handling and the MPRIS service.
class MainWindow : public sigc::trackable
{
public:
    explicit MainWindow(Gtk::Application &app);
    ~MainWindow();
    MainWindow(const MainWindow &) = delete;
    MainWindow &operator=(const MainWindow &) = delete;

    GtkWindow *window() const { return GTK_WINDOW(m_window); }
    void present();
    // Queues files or folders (e.g. from the command line) and plays them.
    void openPaths(const std::vector<std::string> &paths);

private:
    void buildUi();
    void buildActions();
    void connectSignals();

    void chooseLibraryFolder();
    void addFilesToQueue();
    void enqueue(const std::vector<Track> &tracks, LibraryPanel::Action action, int row = -1);
    std::vector<Track> tracksFromPaths(const std::vector<std::string> &paths) const;
    void cycleRepeat();
    void toast(const std::string &message);
    void showAbout();
    bool shortcutAllowed() const;

    void onScanFinished();
    void updateTitle();

    void restoreSession();
    void saveSession();
    void teardown();

    Gtk::Application &m_app;

    std::unique_ptr<PlayQueue> m_queue;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Player> m_player;
    std::unique_ptr<Mpris> m_mpris;
    std::unique_ptr<LibraryPanel> m_libraryPanel;

    GtkWidget *m_window = nullptr;
    GtkWidget *m_toasts = nullptr;
    GtkWidget *m_split = nullptr;
    GtkWidget *m_paned = nullptr;
    GtkWidget *m_windowTitle = nullptr;
    QueuePanel *m_queuePanel = nullptr;
    NowPlayingPanel *m_nowPlaying = nullptr;

    Glib::RefPtr<Gio::SimpleActionGroup> m_actions;
    Glib::RefPtr<Gio::SimpleAction> m_shuffleAction;
    Glib::RefPtr<Gio::SimpleAction> m_lyricsAction;
    Glib::RefPtr<Gio::SimpleAction> m_muteAction;
};
