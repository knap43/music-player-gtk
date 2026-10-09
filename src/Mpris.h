#pragma once

#include <giomm.h>

#include <functional>
#include <set>
#include <string>

class Player;
class PlayQueue;

// org.mpris.MediaPlayer2 on the session bus, so that playerctl, desktop media
// keys and shell media widgets can see and control Amber.
//
// Bus name: org.mpris.MediaPlayer2.amber (playerctl -p amber ...).
class Mpris
{
public:
    struct Hooks {
        std::function<void()> raise;
        std::function<void()> quit;
        std::function<void(const std::string &path)> open;
    };

    Mpris(Player &player, PlayQueue &queue, Hooks hooks);
    ~Mpris();
    Mpris(const Mpris &) = delete;
    Mpris &operator=(const Mpris &) = delete;

private:
    void onBusAcquired(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring &name);
    void onMethodCall(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring &sender,
                      const Glib::ustring &objectPath, const Glib::ustring &interfaceName,
                      const Glib::ustring &methodName, const Glib::VariantContainerBase &parameters,
                      const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation);
    void onGetProperty(Glib::VariantBase &property, const Glib::RefPtr<Gio::DBus::Connection> &connection,
                       const Glib::ustring &sender, const Glib::ustring &objectPath,
                       const Glib::ustring &interfaceName, const Glib::ustring &propertyName);
    bool onSetProperty(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring &sender,
                       const Glib::ustring &objectPath, const Glib::ustring &interfaceName,
                       const Glib::ustring &propertyName, const Glib::VariantBase &value);

    Glib::VariantBase rootProperty(const Glib::ustring &name) const;
    Glib::VariantBase playerProperty(const Glib::ustring &name) const;
    Glib::VariantBase metadata() const;
    std::string trackObjectPath() const;
    void updateArt();

    // Batches PropertiesChanged notifications into one per main-loop turn.
    void markChanged(std::initializer_list<const char *> names);
    void flushChanges();
    void emitSeeked(int64_t positionMs);

    Player &m_player;
    PlayQueue &m_queue;
    Hooks m_hooks;

    Glib::RefPtr<Gio::DBus::NodeInfo> m_introspection;
    Gio::DBus::InterfaceVTable m_vtable;
    Glib::RefPtr<Gio::DBus::Connection> m_connection;
    guint m_ownerId = 0;
    guint m_rootRegistration = 0;
    guint m_playerRegistration = 0;

    std::set<std::string> m_pending;
    sigc::connection m_flush;
    std::string m_artUrl;
    std::string m_generatedArt; // cached PNG we wrote, removed when replaced
    std::vector<sigc::connection> m_connections;
};
