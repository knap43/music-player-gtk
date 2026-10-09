#pragma once

#include <gtkmm.h>

#include <memory>

class MainWindow;

class Application : public Gtk::Application
{
public:
    static Glib::RefPtr<Application> create();
    ~Application() override;

protected:
    Application();
    void on_startup() override;
    void on_activate() override;
    void on_open(const Gio::Application::type_vec_files &files, const Glib::ustring &hint) override;
    void on_shutdown() override;

private:
    MainWindow &window();

    std::unique_ptr<MainWindow> m_window;
};
