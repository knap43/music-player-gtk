#include "Mpris.h"

#include "Library.h"
#include "MetadataReader.h"
#include "PlayQueue.h"
#include "Player.h"

#include "config.h"

#include <glib/gstdio.h>

#include <cmath>
#include <map>

namespace {

const char *const kObjectPath = "/org/mpris/MediaPlayer2";
const char *const kRootInterface = "org.mpris.MediaPlayer2";
const char *const kPlayerInterface = "org.mpris.MediaPlayer2.Player";
const char *const kNoTrack = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

const char *const kIntrospection = R"(
<node>
  <interface name="org.mpris.MediaPlayer2">
    <method name="Raise"/>
    <method name="Quit"/>
    <property name="CanQuit" type="b" access="read"/>
    <property name="CanRaise" type="b" access="read"/>
    <property name="CanSetFullscreen" type="b" access="read"/>
    <property name="Fullscreen" type="b" access="readwrite"/>
    <property name="HasTrackList" type="b" access="read"/>
    <property name="Identity" type="s" access="read"/>
    <property name="DesktopEntry" type="s" access="read"/>
    <property name="SupportedUriSchemes" type="as" access="read"/>
    <property name="SupportedMimeTypes" type="as" access="read"/>
  </interface>
  <interface name="org.mpris.MediaPlayer2.Player">
    <method name="Next"/>
    <method name="Previous"/>
    <method name="Pause"/>
    <method name="PlayPause"/>
    <method name="Stop"/>
    <method name="Play"/>
    <method name="Seek"><arg direction="in" name="Offset" type="x"/></method>
    <method name="SetPosition">
      <arg direction="in" name="TrackId" type="o"/>
      <arg direction="in" name="Position" type="x"/>
    </method>
    <method name="OpenUri"><arg direction="in" name="Uri" type="s"/></method>
    <signal name="Seeked"><arg name="Position" type="x"/></signal>
    <property name="PlaybackStatus" type="s" access="read"/>
    <property name="LoopStatus" type="s" access="readwrite"/>
    <property name="Rate" type="d" access="readwrite"/>
    <property name="Shuffle" type="b" access="readwrite"/>
    <property name="Metadata" type="a{sv}" access="read"/>
    <property name="Volume" type="d" access="readwrite"/>
    <property name="Position" type="x" access="read"/>
    <property name="MinimumRate" type="d" access="read"/>
    <property name="MaximumRate" type="d" access="read"/>
    <property name="CanGoNext" type="b" access="read"/>
    <property name="CanGoPrevious" type="b" access="read"/>
    <property name="CanPlay" type="b" access="read"/>
    <property name="CanPause" type="b" access="read"/>
    <property name="CanSeek" type="b" access="read"/>
    <property name="CanControl" type="b" access="read"/>
  </interface>
</node>
)";

// GVariant strings must be valid UTF-8; file names need not be.
Glib::ustring utf8(const std::string &s)
{
    if (g_utf8_validate(s.data(), gssize(s.size()), nullptr))
        return s;
    gchar *fixed = g_utf8_make_valid(s.data(), gssize(s.size()));
    Glib::ustring out(fixed);
    g_free(fixed);
    return out;
}

std::string fileUri(const std::string &path)
{
    gchar *uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri)
        return {};
    std::string out(uri);
    g_free(uri);
    return out;
}

Glib::VariantBase boolean(bool b)
{
    return Glib::Variant<bool>::create(b);
}

Glib::VariantBase string(const std::string &s)
{
    return Glib::Variant<Glib::ustring>::create(utf8(s));
}

Glib::VariantBase strings(const std::vector<Glib::ustring> &list)
{
    return Glib::Variant<std::vector<Glib::ustring>>::create(list);
}

} // namespace

Mpris::Mpris(Player &player, PlayQueue &queue, Hooks hooks)
    : m_player(player)
    , m_queue(queue)
    , m_hooks(std::move(hooks))
    , m_introspection(Gio::DBus::NodeInfo::create_for_xml(kIntrospection))
    , m_vtable(sigc::mem_fun(*this, &Mpris::onMethodCall), sigc::mem_fun(*this, &Mpris::onGetProperty),
               sigc::mem_fun(*this, &Mpris::onSetProperty))
{
    updateArt();

    m_connections.push_back(m_player.signalTrackChanged.connect([this](const Track &) {
        markChanged({"Metadata"});
    }));
    m_connections.push_back(m_player.signalCoverChanged.connect([this](const Image &) {
        updateArt();
        markChanged({"Metadata"});
    }));
    m_connections.push_back(m_player.signalDurationChanged.connect([this](int64_t) {
        markChanged({"Metadata", "CanSeek"});
    }));
    m_connections.push_back(m_player.signalStatusChanged.connect([this](Player::Status) {
        markChanged({"PlaybackStatus", "CanPlay", "CanPause", "CanSeek", "CanGoNext", "CanGoPrevious"});
    }));
    m_connections.push_back(m_player.signalVolumeChanged.connect([this](int) { markChanged({"Volume"}); }));
    m_connections.push_back(m_player.signalSeeked.connect([this](int64_t ms) { emitSeeked(ms); }));
    m_connections.push_back(m_queue.signalContentsChanged.connect([this] {
        markChanged({"CanGoNext", "CanGoPrevious", "CanPlay"});
    }));
    m_connections.push_back(m_queue.signalCurrentChanged.connect([this](int, uint64_t) {
        markChanged({"CanGoNext", "CanGoPrevious"});
    }));
    m_connections.push_back(m_queue.signalShuffleChanged.connect([this](bool) {
        markChanged({"Shuffle", "CanGoNext", "CanGoPrevious"});
    }));
    m_connections.push_back(m_queue.signalRepeatChanged.connect([this](PlayQueue::Repeat) {
        markChanged({"LoopStatus", "CanGoNext", "CanGoPrevious"});
    }));

    m_ownerId = Gio::DBus::own_name(Gio::DBus::BusType::SESSION, "org.mpris.MediaPlayer2.amber",
                                    sigc::mem_fun(*this, &Mpris::onBusAcquired));
}

Mpris::~Mpris()
{
    for (sigc::connection &c : m_connections)
        c.disconnect();
    m_flush.disconnect();
    if (m_connection) {
        if (m_rootRegistration)
            m_connection->unregister_object(m_rootRegistration);
        if (m_playerRegistration)
            m_connection->unregister_object(m_playerRegistration);
    }
    if (m_ownerId)
        Gio::DBus::unown_name(m_ownerId);
    if (!m_generatedArt.empty())
        g_unlink(m_generatedArt.c_str());
}

void Mpris::onBusAcquired(const Glib::RefPtr<Gio::DBus::Connection> &connection, const Glib::ustring &)
{
    m_connection = connection;
    try {
        m_rootRegistration = connection->register_object(
            kObjectPath, m_introspection->lookup_interface(kRootInterface), m_vtable);
        m_playerRegistration = connection->register_object(
            kObjectPath, m_introspection->lookup_interface(kPlayerInterface), m_vtable);
    } catch (const Glib::Error &e) {
        g_warning("MPRIS: could not register the media player object: %s", e.what());
    }
}

std::string Mpris::trackObjectPath() const
{
    if (!m_player.hasTrack())
        return kNoTrack;
    return "/io/github/knap43/Amber/track/" + std::to_string(m_player.currentEntryId());
}

void Mpris::updateArt()
{
    std::string url, generated;
    if (m_player.hasTrack() && m_player.cover()) {
        const std::string &path = m_player.currentTrack().path;
        const std::string folderCover = Metadata::folderCoverPath(Paths::dirPath(path));
        if (!folderCover.empty()) {
            url = fileUri(folderCover);
        } else {
            // Embedded artwork: write it out once, named after the track.
            const std::string dir = Library::cacheDir() + "/mpris";
            g_mkdir_with_parents(dir.c_str(), 0755);
            gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA1, path.c_str(), -1);
            const std::string file = dir + "/" + hash + ".png";
            g_free(hash);
            if (file == m_generatedArt || ImageUtils::savePng(ImageUtils::fitWithin(m_player.cover(), 512), file)) {
                generated = file;
                url = fileUri(file);
            }
        }
    }
    if (!m_generatedArt.empty() && m_generatedArt != generated)
        g_unlink(m_generatedArt.c_str());
    m_generatedArt = generated;
    m_artUrl = url;
}

Glib::VariantBase Mpris::metadata() const
{
    std::map<Glib::ustring, Glib::VariantBase> md;
    md["mpris:trackid"] = Glib::Variant<Glib::DBusObjectPathString>::create(trackObjectPath());
    if (m_player.hasTrack()) {
        const Track &t = m_player.currentTrack();
        md["mpris:length"] = Glib::Variant<gint64>::create(m_player.duration() * 1000);
        md["xesam:title"] = string(t.displayTitle());
        md["xesam:album"] = string(t.displayAlbum());
        if (!t.artist.empty())
            md["xesam:artist"] = strings({utf8(t.artist)});
        if (t.trackNumber > 0)
            md["xesam:trackNumber"] = Glib::Variant<gint32>::create(t.trackNumber);
        if (t.discNumber > 0)
            md["xesam:discNumber"] = Glib::Variant<gint32>::create(t.discNumber);
        const std::string url = fileUri(t.path);
        if (!url.empty())
            md["xesam:url"] = string(url);
        if (!m_artUrl.empty())
            md["mpris:artUrl"] = string(m_artUrl);
    }
    return Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>::create(md);
}

Glib::VariantBase Mpris::rootProperty(const Glib::ustring &name) const
{
    if (name == "CanQuit" || name == "CanRaise")
        return boolean(true);
    if (name == "CanSetFullscreen" || name == "Fullscreen" || name == "HasTrackList")
        return boolean(false);
    if (name == "Identity")
        return string("Amber");
    if (name == "DesktopEntry")
        return string(AMBER_APP_ID);
    if (name == "SupportedUriSchemes")
        return strings({"file"});
    if (name == "SupportedMimeTypes")
        return strings({"audio/mpeg", "audio/flac", "audio/x-flac", "audio/ogg", "audio/x-vorbis+ogg",
                        "audio/vorbis", "audio/x-opus+ogg", "audio/opus", "audio/webm", "video/webm"});
    return {};
}

Glib::VariantBase Mpris::playerProperty(const Glib::ustring &name) const
{
    if (name == "PlaybackStatus") {
        switch (m_player.status()) {
        case Player::Status::Playing:
            return string("Playing");
        case Player::Status::Paused:
            return string("Paused");
        case Player::Status::Stopped:
            return string("Stopped");
        }
    }
    if (name == "LoopStatus") {
        switch (m_queue.repeat()) {
        case PlayQueue::Repeat::Off:
            return string("None");
        case PlayQueue::Repeat::One:
            return string("Track");
        case PlayQueue::Repeat::All:
            return string("Playlist");
        }
    }
    if (name == "Rate" || name == "MinimumRate" || name == "MaximumRate")
        return Glib::Variant<double>::create(1.0);
    if (name == "Shuffle")
        return boolean(m_queue.shuffle());
    if (name == "Metadata")
        return metadata();
    if (name == "Volume")
        return Glib::Variant<double>::create(m_player.volume() / 100.0);
    if (name == "Position")
        return Glib::Variant<gint64>::create(m_player.position() * 1000);
    if (name == "CanGoNext")
        return boolean(m_queue.hasNext());
    if (name == "CanGoPrevious")
        return boolean(m_player.hasTrack() || m_queue.hasPrevious());
    if (name == "CanPlay")
        return boolean(m_player.hasTrack() || !m_queue.isEmpty());
    if (name == "CanPause")
        return boolean(m_player.hasTrack());
    if (name == "CanSeek")
        return boolean(m_player.canSeek());
    if (name == "CanControl")
        return boolean(true);
    return {};
}

void Mpris::onGetProperty(Glib::VariantBase &property, const Glib::RefPtr<Gio::DBus::Connection> &,
                          const Glib::ustring &, const Glib::ustring &, const Glib::ustring &interfaceName,
                          const Glib::ustring &propertyName)
{
    property = interfaceName == kRootInterface ? rootProperty(propertyName) : playerProperty(propertyName);
    if (!property)
        throw Gio::DBus::Error(Gio::DBus::Error::UNKNOWN_PROPERTY, "Unknown property " + propertyName);
}

bool Mpris::onSetProperty(const Glib::RefPtr<Gio::DBus::Connection> &, const Glib::ustring &,
                          const Glib::ustring &, const Glib::ustring &interfaceName,
                          const Glib::ustring &propertyName, const Glib::VariantBase &value)
{
    GVariant *v = const_cast<GVariant *>(value.gobj());
    if (interfaceName != kPlayerInterface)
        return true; // Fullscreen: not supported, silently ignored

    if (propertyName == "LoopStatus" && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
        const std::string mode = g_variant_get_string(v, nullptr);
        if (mode == "None")
            m_queue.setRepeat(PlayQueue::Repeat::Off);
        else if (mode == "Track")
            m_queue.setRepeat(PlayQueue::Repeat::One);
        else if (mode == "Playlist")
            m_queue.setRepeat(PlayQueue::Repeat::All);
        else
            throw Gio::DBus::Error(Gio::DBus::Error::INVALID_ARGS, "LoopStatus must be None, Track or Playlist");
    } else if (propertyName == "Shuffle" && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)) {
        m_queue.setShuffle(g_variant_get_boolean(v));
    } else if (propertyName == "Volume" && g_variant_is_of_type(v, G_VARIANT_TYPE_DOUBLE)) {
        const double volume = g_variant_get_double(v);
        m_player.setVolume(int(std::lround(std::clamp(volume, 0.0, 1.0) * 100)));
        // Report back even if the rounded value did not change.
        markChanged({"Volume"});
    } else if (propertyName == "Rate") {
        // Only 1.0 is supported; other rates are ignored as the spec allows.
    }
    return true;
}

void Mpris::onMethodCall(const Glib::RefPtr<Gio::DBus::Connection> &, const Glib::ustring &,
                         const Glib::ustring &, const Glib::ustring &interfaceName,
                         const Glib::ustring &methodName, const Glib::VariantContainerBase &parameters,
                         const Glib::RefPtr<Gio::DBus::MethodInvocation> &invocation)
{
    if (interfaceName == kRootInterface) {
        if (methodName == "Raise" && m_hooks.raise)
            m_hooks.raise();
        else if (methodName == "Quit" && m_hooks.quit)
            Glib::signal_idle().connect_once(m_hooks.quit); // reply first, then quit
        invocation->return_value(Glib::VariantContainerBase());
        return;
    }

    if (methodName == "Next") {
        m_player.next();
    } else if (methodName == "Previous") {
        m_player.previous();
    } else if (methodName == "Pause") {
        m_player.pause();
    } else if (methodName == "PlayPause") {
        m_player.togglePlayPause();
    } else if (methodName == "Stop") {
        m_player.stop();
    } else if (methodName == "Play") {
        m_player.play();
    } else if (methodName == "Seek") {
        Glib::Variant<gint64> offset;
        parameters.get_child(offset, 0);
        if (m_player.canSeek()) {
            const int64_t target = m_player.position() + offset.get() / 1000;
            if (target >= m_player.duration())
                m_player.next(); // as the spec says
            else
                m_player.seek(std::max<int64_t>(0, target));
        }
    } else if (methodName == "SetPosition") {
        Glib::Variant<Glib::DBusObjectPathString> trackId;
        Glib::Variant<gint64> position;
        parameters.get_child(trackId, 0);
        parameters.get_child(position, 1);
        const int64_t ms = position.get() / 1000;
        // Stale track ids and out-of-range positions are ignored, per the spec.
        if (m_player.canSeek() && std::string(trackId.get()) == trackObjectPath() && ms >= 0
            && ms <= m_player.duration())
            m_player.seek(ms);
    } else if (methodName == "OpenUri") {
        Glib::Variant<Glib::ustring> uri;
        parameters.get_child(uri, 0);
        gchar *path = g_filename_from_uri(uri.get().c_str(), nullptr, nullptr);
        if (!path) {
            invocation->return_dbus_error("org.freedesktop.DBus.Error.InvalidArgs",
                                          "Only local file:// URIs are supported");
            return;
        }
        const std::string local(path);
        g_free(path);
        if (m_hooks.open)
            m_hooks.open(local);
    } else {
        invocation->return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod",
                                      "Unknown method " + methodName);
        return;
    }
    invocation->return_value(Glib::VariantContainerBase());
}

void Mpris::markChanged(std::initializer_list<const char *> names)
{
    for (const char *name : names)
        m_pending.insert(name);
    if (!m_flush.connected())
        m_flush = Glib::signal_idle().connect([this] {
            flushChanges();
            return false;
        });
}

void Mpris::flushChanges()
{
    if (!m_connection || m_pending.empty()) {
        m_pending.clear();
        return;
    }
    std::map<Glib::ustring, Glib::VariantBase> changed;
    for (const std::string &name : m_pending)
        if (Glib::VariantBase value = playerProperty(name))
            changed[name] = value;
    m_pending.clear();

    const auto params = Glib::VariantContainerBase::create_tuple(
        {Glib::Variant<Glib::ustring>::create(kPlayerInterface),
         Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>::create(changed),
         Glib::Variant<std::vector<Glib::ustring>>::create({})});
    try {
        m_connection->emit_signal(kObjectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged", {},
                                  params);
    } catch (const Glib::Error &e) {
        g_warning("MPRIS: %s", e.what());
    }
}

void Mpris::emitSeeked(int64_t positionMs)
{
    if (!m_connection)
        return;
    try {
        m_connection->emit_signal(kObjectPath, kPlayerInterface, "Seeked", {},
                                  Glib::VariantContainerBase::create_tuple(
                                      {Glib::Variant<gint64>::create(positionMs * 1000)}));
    } catch (const Glib::Error &e) {
        g_warning("MPRIS: %s", e.what());
    }
}
