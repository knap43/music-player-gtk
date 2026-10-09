#include "DragData.h"

#include <sstream>

namespace {

const char *const kQueuePrefix = "amber-queue\n";
const char *const kPathsPrefix = "amber-paths\n";

std::vector<std::string> splitLines(const std::string &s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            out.push_back(line);
    }
    return out;
}

bool startsWith(const std::string &s, const char *prefix)
{
    return s.rfind(prefix, 0) == 0;
}

GdkContentProvider *forString(const std::string &text)
{
    return gdk_content_provider_new_typed(G_TYPE_STRING, text.c_str());
}

} // namespace

namespace DragData {

GdkContentProvider *forPaths(const std::vector<std::string> &paths)
{
    std::string text = kPathsPrefix;
    for (const std::string &p : paths)
        text += p + "\n";
    return forString(text);
}

GdkContentProvider *forQueueIds(const std::vector<uint64_t> &ids)
{
    std::string text = kQueuePrefix;
    for (uint64_t id : ids)
        text += std::to_string(id) + "\n";
    return forString(text);
}

Payload decode(const GValue *value)
{
    Payload payload;
    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        GSList *files = gdk_file_list_get_files(static_cast<GdkFileList *>(g_value_get_boxed(value)));
        for (GSList *l = files; l; l = l->next) {
            if (gchar *path = g_file_get_path(G_FILE(l->data))) {
                payload.paths.emplace_back(path);
                g_free(path);
            }
        }
        g_slist_free(files);
        return payload;
    }
    if (!G_VALUE_HOLDS_STRING(value) || !g_value_get_string(value))
        return payload;

    const std::string text = g_value_get_string(value);
    if (startsWith(text, kQueuePrefix)) {
        for (const std::string &line : splitLines(text.substr(std::char_traits<char>::length(kQueuePrefix))))
            payload.queueIds.push_back(std::stoull(line));
    } else if (startsWith(text, kPathsPrefix)) {
        payload.paths = splitLines(text.substr(std::char_traits<char>::length(kPathsPrefix)));
    } else {
        // A text/uri-list from elsewhere.
        for (const std::string &line : splitLines(text)) {
            if (!startsWith(line, "file://"))
                continue;
            if (gchar *path = g_filename_from_uri(line.c_str(), nullptr, nullptr)) {
                payload.paths.emplace_back(path);
                g_free(path);
            }
        }
    }
    return payload;
}

GtkDropTarget *newDropTarget()
{
    GtkDropTarget *target = gtk_drop_target_new(G_TYPE_INVALID, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE));
    GType types[] = {GDK_TYPE_FILE_LIST, G_TYPE_STRING};
    gtk_drop_target_set_gtypes(target, types, G_N_ELEMENTS(types));
    gtk_drop_target_set_preload(target, FALSE);
    return target;
}

void useWidgetAsIcon(GtkDragSource *source, GtkWidget *widget)
{
    g_signal_connect(source, "drag-begin", G_CALLBACK(+[](GtkDragSource *src, GdkDrag *, gpointer w) {
                         GdkPaintable *paintable = gtk_widget_paintable_new(GTK_WIDGET(w));
                         gtk_drag_source_set_icon(src, paintable, 0, 0);
                         g_object_unref(paintable);
                     }),
                     widget);
}

} // namespace DragData
