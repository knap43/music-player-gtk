#include "QueuePanel.h"

#include <adwaita.h>

#include <algorithm>

// One row of the queue list.
class QueueRow : public Gtk::Box
{
public:
    explicit QueueRow(QueuePanel &panel)
        : Gtk::Box(Gtk::Orientation::HORIZONTAL, 10)
        , m_panel(panel)
        , m_text(Gtk::Orientation::VERTICAL, 2)
    {
        add_css_class("queue-row");
        m_indicator.add_css_class("indicator");
        m_indicator.add_css_class("accent");
        m_indicator.set_pixel_size(16);
        for (Gtk::Label *l : {&m_title, &m_subtitle}) {
            l->set_xalign(0);
            l->set_ellipsize(Pango::EllipsizeMode::END);
        }
        m_title.add_css_class("title");
        m_subtitle.add_css_class("caption");
        m_subtitle.add_css_class("dim-label");
        m_text.set_hexpand(true);
        m_text.set_valign(Gtk::Align::CENTER);
        m_text.append(m_title);
        m_text.append(m_subtitle);
        m_duration.add_css_class("numeric");
        m_duration.add_css_class("dim-label");
        m_duration.add_css_class("caption");
        append(m_indicator);
        append(m_text);
        append(m_duration);

        GtkDragSource *source = gtk_drag_source_new();
        gtk_drag_source_set_actions(source, GDK_ACTION_COPY);
        g_signal_connect(source, "prepare", G_CALLBACK(+[](GtkDragSource *, double, double, gpointer self) {
                             auto *row = static_cast<QueueRow *>(self);
                             if (row->m_position < 0)
                                 return static_cast<GdkContentProvider *>(nullptr);
                             return DragData::forQueueIds(row->m_panel.dragIdsFor(row->m_position));
                         }),
                         this);
        DragData::useWidgetAsIcon(source, GTK_WIDGET(gobj()));
        gtk_widget_add_controller(GTK_WIDGET(gobj()), GTK_EVENT_CONTROLLER(source));

        GtkDropTarget *target = DragData::newDropTarget();
        g_signal_connect(target, "drop", G_CALLBACK(+[](GtkDropTarget *, const GValue *value, double, double y,
                                                         gpointer self) -> gboolean {
                             auto *row = static_cast<QueueRow *>(self);
                             if (row->m_position < 0)
                                 return FALSE;
                             const int at = row->m_position + (y > row->get_height() / 2.0 ? 1 : 0);
                             return row->m_panel.handleDrop(value, at);
                         }),
                         this);
        gtk_widget_add_controller(GTK_WIDGET(gobj()), GTK_EVENT_CONTROLLER(target));
    }

    void bind(const Track &track, int position, bool current, bool playing)
    {
        m_position = position;
        m_title.set_text(track.displayTitle());
        m_subtitle.set_text(track.displayArtist() + " — " + track.displayAlbum());
        m_duration.set_text(Format::duration(track.durationMs));
        set_tooltip_text(track.displayTitle() + "\n" + track.displayArtist() + " — " + track.displayAlbum()
                         + "\n" + track.path);
        if (current) {
            add_css_class("current");
            m_title.add_css_class("accent");
            m_indicator.set_from_icon_name(playing ? "media-playback-start-symbolic"
                                                   : "media-playback-pause-symbolic");
        } else {
            remove_css_class("current");
            m_title.remove_css_class("accent");
            m_indicator.set_from_icon_name("");
        }
    }

    void unbind() { m_position = -1; }

private:
    QueuePanel &m_panel;
    Gtk::Image m_indicator;
    Gtk::Box m_text;
    Gtk::Label m_title;
    Gtk::Label m_subtitle;
    Gtk::Label m_duration;
    int m_position = -1;
};

QueuePanel::QueuePanel(PlayQueue &queue)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0)
    , m_queue(queue)
    , m_model(Gtk::StringList::create({}))
    , m_selection(Gtk::MultiSelection::create(m_model))
{
    set_size_request(280, -1);

    // Header: title, totals and the edit buttons.
    auto *header = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    header->set_margin_start(14);
    header->set_margin_end(8);
    header->set_margin_top(10);
    header->set_margin_bottom(6);
    auto *title = Gtk::make_managed<Gtk::Label>("Queue");
    title->add_css_class("heading");
    m_info.add_css_class("dim-label");
    m_info.add_css_class("caption");
    m_info.add_css_class("numeric");
    m_info.set_hexpand(true);
    m_info.set_xalign(0);
    m_remove.set_icon_name("list-remove-symbolic");
    m_remove.set_tooltip_text("Remove selected (Delete)");
    m_clear.set_icon_name("edit-clear-all-symbolic");
    m_clear.set_tooltip_text("Clear the queue");
    for (Gtk::Button *b : {&m_remove, &m_clear})
        b->add_css_class("flat");
    m_remove.signal_clicked().connect([this] { removeSelected(); });
    m_clear.signal_clicked().connect([this] { m_queue.clear(); });
    header->append(*title);
    header->append(m_info);
    header->append(m_remove);
    header->append(m_clear);
    append(*header);

    // The list.
    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem> &item) {
        item->set_child(*Gtk::make_managed<QueueRow>(*this));
    });
    factory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem> &item) {
        if (auto *row = dynamic_cast<QueueRow *>(item->get_child()))
            bindRow(*row, int(item->get_position()));
    });
    factory->signal_unbind().connect([](const Glib::RefPtr<Gtk::ListItem> &item) {
        if (auto *row = dynamic_cast<QueueRow *>(item->get_child()))
            row->unbind();
    });
    m_list.set_model(m_selection);
    m_list.set_factory(factory);
    m_list.add_css_class("navigation-sidebar");
    m_list.add_css_class("queue");
    m_list.signal_activate().connect([this](guint position) { signalRowActivated.emit(int(position)); });
    m_selection->signal_selection_changed().connect([this](guint, guint) { updateButtons(); });

    auto keys = Gtk::ShortcutController::create();
    keys->add_shortcut(Gtk::Shortcut::create(
        Gtk::ShortcutTrigger::parse_string("Delete|KP_Delete"),
        Gtk::CallbackAction::create([this](Gtk::Widget &, const Glib::VariantBase &) {
            removeSelected();
            return true;
        })));
    m_list.add_controller(keys);

    m_scroller.set_child(m_list);
    m_scroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroller.set_vexpand(true);

    GtkWidget *empty = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "view-list-symbolic");
    adw_status_page_set_title(ADW_STATUS_PAGE(empty), "Queue Is Empty");
    adw_status_page_set_description(ADW_STATUS_PAGE(empty),
                                    "Add albums or tracks from the library, or drop music files here");
    gtk_widget_add_css_class(empty, "compact");

    gtk_stack_add_named(m_stack.gobj(), empty, "empty");
    m_stack.add(m_scroller, "list");
    m_stack.set_vexpand(true);
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    append(m_stack);

    // Drops on empty space append to the queue.
    GtkDropTarget *target = DragData::newDropTarget();
    g_signal_connect(target, "drop", G_CALLBACK(+[](GtkDropTarget *, const GValue *value, double, double,
                                                     gpointer self) -> gboolean {
                         return static_cast<QueuePanel *>(self)->handleDrop(value, -1);
                     }),
                     this);
    gtk_widget_add_controller(GTK_WIDGET(m_stack.gobj()), GTK_EVENT_CONTROLLER(target));

    m_queue.signalRowsInserted.connect(sigc::mem_fun(*this, &QueuePanel::onRowsInserted));
    m_queue.signalRowsRemoved.connect(sigc::mem_fun(*this, &QueuePanel::onRowsRemoved));
    m_queue.signalReset.connect(sigc::mem_fun(*this, &QueuePanel::rebuild));
    m_queue.signalRowUpdated.connect(sigc::mem_fun(*this, &QueuePanel::onRowUpdated));
    m_queue.signalCurrentChanged.connect(sigc::mem_fun(*this, &QueuePanel::onCurrentChanged));
    m_queue.signalContentsChanged.connect(sigc::mem_fun(*this, &QueuePanel::updateInfo));

    rebuild();
}

void QueuePanel::bindRow(QueueRow &row, int position)
{
    if (position < 0 || position >= m_queue.count())
        return;
    const bool current = m_queue.idAt(position) == m_queue.currentId() && m_queue.currentId() != 0;
    row.bind(m_queue.trackAt(position), position, current, m_playing);
}

void QueuePanel::rebuild()
{
    std::vector<Glib::ustring> ids;
    ids.reserve(size_t(m_queue.count()));
    for (int i = 0; i < m_queue.count(); ++i)
        ids.push_back(std::to_string(m_queue.idAt(i)));
    m_model->splice(0, m_model->get_n_items(), ids);
    updateInfo();
}

void QueuePanel::onRowsInserted(int row, int count)
{
    std::vector<Glib::ustring> ids;
    for (int i = row; i < row + count; ++i)
        ids.push_back(std::to_string(m_queue.idAt(i)));
    m_model->splice(guint(row), 0, ids);
}

void QueuePanel::onRowsRemoved(int row, int count)
{
    m_model->splice(guint(row), guint(count), {});
}

void QueuePanel::refreshRow(int row)
{
    if (row < 0 || row >= int(m_model->get_n_items()) || row >= m_queue.count())
        return;
    // Re-inserting the same item makes the list view rebind the row.
    const bool selected = m_selection->is_selected(guint(row));
    m_model->splice(guint(row), 1, {std::to_string(m_queue.idAt(row))});
    if (selected)
        m_selection->select_item(guint(row), false);
}

void QueuePanel::onRowUpdated(int row)
{
    refreshRow(row);
}

void QueuePanel::onCurrentChanged(int row, uint64_t previousId)
{
    const int previous = m_queue.rowOfId(previousId);
    if (previous >= 0 && previous != row)
        refreshRow(previous);
    if (row >= 0) {
        refreshRow(row);
        scrollToCurrent();
    }
}

void QueuePanel::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    refreshRow(m_queue.currentRow());
}

void QueuePanel::scrollToCurrent()
{
    const int row = m_queue.currentRow();
    if (row < 0)
        return;
#if GTK_CHECK_VERSION(4, 12, 0)
    gtk_list_view_scroll_to(m_list.gobj(), guint(row), GTK_LIST_SCROLL_NONE, nullptr);
#endif
}

void QueuePanel::updateInfo()
{
    if (m_queue.isEmpty()) {
        m_info.set_text("");
        m_stack.set_visible_child("empty");
    } else {
        const int n = m_queue.count();
        m_info.set_text((n == 1 ? std::string("1 track") : std::to_string(n) + " tracks") + " · "
                        + Format::duration(m_queue.totalDurationMs()));
        m_stack.set_visible_child("list");
    }
    updateButtons();
}

void QueuePanel::updateButtons()
{
    m_remove.set_sensitive(!selectedRows().empty());
    m_clear.set_sensitive(!m_queue.isEmpty());
}

std::vector<int> QueuePanel::selectedRows() const
{
    std::vector<int> rows;
    GtkBitset *set = gtk_selection_model_get_selection(GTK_SELECTION_MODEL(m_selection->gobj()));
    const guint64 n = gtk_bitset_get_size(set);
    for (guint64 i = 0; i < n; ++i)
        rows.push_back(int(gtk_bitset_get_nth(set, guint(i))));
    gtk_bitset_unref(set);
    return rows;
}

void QueuePanel::removeSelected()
{
    const std::vector<int> rows = selectedRows();
    if (rows.empty())
        return;
    m_queue.removeRows(rows);
    // Keep a selection near the removed rows, so Delete can be pressed again.
    if (!m_queue.isEmpty()) {
        const int next = std::min(*std::min_element(rows.begin(), rows.end()), m_queue.count() - 1);
        m_selection->select_item(guint(next), true);
    }
}

std::vector<uint64_t> QueuePanel::dragIdsFor(int row) const
{
    std::vector<uint64_t> ids;
    std::vector<int> rows = selectedRows();
    if (std::find(rows.begin(), rows.end(), row) == rows.end())
        rows = {row};
    for (int r : rows)
        if (r >= 0 && r < m_queue.count())
            ids.push_back(m_queue.idAt(r));
    return ids;
}

bool QueuePanel::handleDrop(const GValue *value, int row)
{
    const DragData::Payload payload = DragData::decode(value);
    if (!payload.queueIds.empty()) {
        std::vector<int> rows;
        for (uint64_t id : payload.queueIds) {
            const int r = m_queue.rowOfId(id);
            if (r >= 0)
                rows.push_back(r);
        }
        const int first = m_queue.moveRowsTo(rows, row < 0 ? m_queue.count() : row);
        if (first >= 0) {
            m_selection->unselect_all();
            for (size_t i = 0; i < rows.size(); ++i)
                m_selection->select_item(guint(first) + guint(i), false);
        }
        return first >= 0;
    }
    if (!payload.paths.empty()) {
        signalPathsDropped.emit(payload.paths, row);
        return true;
    }
    return false;
}
