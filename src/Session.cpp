#include "Session.h"

#include <glib.h>
#include <glib/gstdio.h>

namespace {

int getInt(GKeyFile *kf, const char *group, const char *key, int fallback)
{
    GError *error = nullptr;
    const int value = g_key_file_get_integer(kf, group, key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

bool getBool(GKeyFile *kf, const char *group, const char *key, bool fallback)
{
    GError *error = nullptr;
    const gboolean value = g_key_file_get_boolean(kf, group, key, &error);
    if (error) {
        g_error_free(error);
        return fallback;
    }
    return value;
}

std::string getString(GKeyFile *kf, const char *group, const char *key)
{
    gchar *value = g_key_file_get_string(kf, group, key, nullptr);
    if (!value)
        return {};
    std::string out(value);
    g_free(value);
    return out;
}

} // namespace

std::string Session::path()
{
    return std::string(g_get_user_config_dir()) + "/amber-player/session.ini";
}

Session Session::load()
{
    Session s;
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path().c_str(), G_KEY_FILE_NONE, nullptr)) {
        g_key_file_unref(kf);
        return s;
    }
    s.width = getInt(kf, "window", "width", s.width);
    s.height = getInt(kf, "window", "height", s.height);
    s.maximized = getBool(kf, "window", "maximized", s.maximized);
    s.panedPosition = getInt(kf, "window", "paned", s.panedPosition);
    s.showSidebar = getBool(kf, "window", "sidebar", s.showSidebar);
    s.libraryRoot = getString(kf, "library", "root");
    s.volume = getInt(kf, "player", "volume", s.volume);
    s.muted = getBool(kf, "player", "muted", s.muted);
    s.shuffle = getBool(kf, "player", "shuffle", s.shuffle);
    s.repeat = getInt(kf, "player", "repeat", s.repeat);
    s.lyrics = getBool(kf, "view", "lyrics", s.lyrics);

    gsize n = 0;
    if (gchar **paths = g_key_file_get_string_list(kf, "queue", "paths", &n, nullptr)) {
        for (gsize i = 0; i < n; ++i)
            s.queuePaths.emplace_back(paths[i]);
        g_strfreev(paths);
    }
    s.queueCurrent = getInt(kf, "queue", "current", -1);
    GError *error = nullptr;
    s.queuePosition = g_key_file_get_int64(kf, "queue", "position", &error);
    if (error) {
        g_error_free(error);
        s.queuePosition = 0;
    }
    g_key_file_unref(kf);
    return s;
}

void Session::save() const
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_integer(kf, "window", "width", width);
    g_key_file_set_integer(kf, "window", "height", height);
    g_key_file_set_boolean(kf, "window", "maximized", maximized);
    g_key_file_set_integer(kf, "window", "paned", panedPosition);
    g_key_file_set_boolean(kf, "window", "sidebar", showSidebar);
    g_key_file_set_string(kf, "library", "root", libraryRoot.c_str());
    g_key_file_set_integer(kf, "player", "volume", volume);
    g_key_file_set_boolean(kf, "player", "muted", muted);
    g_key_file_set_boolean(kf, "player", "shuffle", shuffle);
    g_key_file_set_integer(kf, "player", "repeat", repeat);
    g_key_file_set_boolean(kf, "view", "lyrics", lyrics);

    std::vector<const gchar *> paths;
    for (const std::string &p : queuePaths)
        paths.push_back(p.c_str());
    g_key_file_set_string_list(kf, "queue", "paths", paths.data(), paths.size());
    g_key_file_set_integer(kf, "queue", "current", queueCurrent);
    g_key_file_set_int64(kf, "queue", "position", queuePosition);

    const std::string file = path();
    gchar *dir = g_path_get_dirname(file.c_str());
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    g_key_file_save_to_file(kf, file.c_str(), nullptr);
    g_key_file_unref(kf);
}
