#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Everything that survives a restart, stored as a key file in
// ~/.config/amber-player/session.ini.
struct Session {
    int width = 1320;
    int height = 820;
    bool maximized = false;
    int panedPosition = -1;
    bool showSidebar = true;

    std::string libraryRoot;
    int volume = 80;
    bool muted = false;
    bool shuffle = false;
    int repeat = 0; // PlayQueue::Repeat
    bool lyrics = true;

    std::vector<std::string> queuePaths;
    int queueCurrent = -1;
    int64_t queuePosition = 0;

    static std::string path();
    static Session load();
    void save() const;
};
