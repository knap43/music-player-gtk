#pragma once

#include "DragData.h"
#include "PlayQueue.h"

#include <gtkmm.h>

class QueueRow;

// The play queue: a list view mirroring PlayQueue, with reordering by drag and
// drop, drops from the library and file managers, and Delete to remove.
class QueuePanel : public Gtk::Box
{
public:
    explicit QueuePanel(PlayQueue &queue);
    ~QueuePanel() override;

    void setPlaying(bool playing);
    void scrollToCurrent();
    std::vector<int> selectedRows() const;
    void removeSelected();

    sigc::signal<void(int)> signalRowActivated;
    // Files or folders dropped at a queue row (-1 = append).
    sigc::signal<void(const std::vector<std::string> &, int)> signalPathsDropped;

private:
    friend class QueueRow;

    void rebuild();
    void refreshRow(int row);
    void updateInfo();
    void updateButtons();
    bool handleDrop(const GValue *value, int row);
    std::vector<uint64_t> dragIdsFor(uint64_t id) const;
    void bindRow(QueueRow &row, int position);

    // Drops on the list: where a point (in list coordinates) would insert,
    // -1 meaning the end, plus the row widget to mark and on which edge.
    int dropRowAt(double y, GtkWidget **item, bool *below) const;
    void updateDropIndicator(double y);
    void clearDropIndicator();
    void setAutoscroll(double y);
    void stopAutoscroll();

    void onRowsInserted(int row, int count);
    void onRowsRemoved(int row, int count);
    void onRowUpdated(int row);
    void onCurrentChanged(int row, uint64_t previousId);

    PlayQueue &m_queue;
    Glib::RefPtr<Gtk::StringList> m_model;
    Glib::RefPtr<Gtk::MultiSelection> m_selection;
    Gtk::Label m_info;
    Gtk::Button m_remove;
    Gtk::Button m_clear;
    Gtk::Stack m_stack;
    Gtk::ScrolledWindow m_scroller;
    Gtk::ListView m_list;
    bool m_playing = false;

    GtkWidget *m_dropItem = nullptr; // weak
    double m_dropY = 0;
    double m_autoscrollSpeed = 0;
    guint m_autoscrollSource = 0;
};
