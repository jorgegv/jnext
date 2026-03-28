#pragma once

#include <QWidget>
#include <QTableWidget>
#include <vector>
#include <cstdint>
#include <string>

class Emulator;

/// Watch panel showing memory values at watched addresses.
class WatchPanel : public QWidget {
    Q_OBJECT
public:
    explicit WatchPanel(Emulator* emulator, QWidget* parent = nullptr);

    /// Refresh all watch values from current emulator memory state.
    void refresh();

    /// Add a watch entry.
    void add_watch(uint16_t addr, const std::string& label, int type = 0);

    /// Remove the currently selected watch.
    void remove_selected();

    /// Get number of watches.
    int watch_count() const { return static_cast<int>(watches_.size()); }

public slots:
    /// Show dialog to add a new watch.
    void on_add_watch();

    /// Show dialog to edit the currently selected watch.
    void on_edit_watch();

private:
    void update_table();
    bool show_watch_dialog(const QString& title, uint16_t& addr,
                           std::string& label, int& type);

    Emulator* emulator_;
    QTableWidget* table_ = nullptr;

    enum WatchType { BYTE = 0, WORD = 1, LONG = 2 };

    struct WatchEntry {
        uint16_t addr;
        std::string label;
        WatchType type;
    };

    std::vector<WatchEntry> watches_;
};
