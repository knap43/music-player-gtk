#include "Application.h"

#include "MainWindow.h"
#include "Theme.h"

#include "config.h"

#include <adwaita.h>

Glib::RefPtr<Application> Application::create()
{
    return Glib::make_refptr_for_instance<Application>(new Application());
}

Application::Application()
    : Gtk::Application(AMBER_APP_ID, Gio::Application::Flags::HANDLES_OPEN)
{
    Glib::set_application_name("Amber");
}

Application::~Application() = default;

void Application::on_startup()
{
    Gtk::Application::on_startup();
    adw_init();
    gtk_window_set_default_icon_name(AMBER_APP_ID);
    Theme::install();

    add_action("quit", [this] {
        if (m_window)
            gtk_window_close(m_window->window());
        else
            quit();
    });
}

MainWindow &Application::window()
{
    if (!m_window)
        m_window = std::make_unique<MainWindow>(*this);
    return *m_window;
}

void Application::on_activate()
{
    window().present();
}

void Application::on_open(const Gio::Application::type_vec_files &files, const Glib::ustring &)
{
    MainWindow &win = window();
    win.present();
    std::vector<std::string> paths;
    for (const auto &file : files) {
        const std::string path = file->get_path();
        if (!path.empty())
            paths.push_back(path);
    }
    win.openPaths(paths);
}

void Application::on_shutdown()
{
    m_window.reset();
    Gtk::Application::on_shutdown();
}
