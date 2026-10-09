#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <string>
#include <vector>

// Drag-and-drop payloads. Library rows and queue rows carry plain strings with
// a prefix; file managers offer a GdkFileList (or, rarely, a uri-list string).
namespace DragData {

struct Payload {
    std::vector<uint64_t> queueIds;  // rows dragged within the queue
    std::vector<std::string> paths;  // files or folders to add
};

GdkContentProvider *forPaths(const std::vector<std::string> &paths);
GdkContentProvider *forQueueIds(const std::vector<uint64_t> &ids);
Payload decode(const GValue *value);

// A drop target that accepts everything above.
GtkDropTarget *newDropTarget();
// Uses a picture of `widget` as the drag icon.
void useWidgetAsIcon(GtkDragSource *source, GtkWidget *widget);

} // namespace DragData
