#pragma once

#include <glib.h>

#include <functional>
#include <utility>

namespace MainThread {

// Runs `fn` on the default GLib main context. Safe to call from any thread.
inline void post(std::function<void()> fn)
{
    auto *heap = new std::function<void()>(std::move(fn));
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()> *>(data))();
            return G_SOURCE_REMOVE;
        },
        heap, [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

} // namespace MainThread
