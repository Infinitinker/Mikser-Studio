#pragma once

#include <QFrame>

class QLineEdit;
class QListView;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

class CommandPaletteModel;

// The command palette (Ctrl+Shift+P / Ctrl+K in the Shell): a popup with a fuzzy search over
// every registered command (title, id, category) that shows each command's shortcut. Enter runs
// the selected command, after asking for its params when it has required ones.
class CommandPalette final : public QFrame {
    Q_OBJECT

public:
    CommandPalette(core::CommandBus& bus, QWidget* window);
    ~CommandPalette() override;

    // Reloads the commands, clears the query and shows the palette at the top of the window.
    void popup();
    void setQuery(const QString& query);
    [[nodiscard]] QStringList visibleCommandIds() const;
    [[nodiscard]] QString currentCommandId() const;
    // Hides the palette and runs the selected command.
    void runCurrent();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void moveSelection(int delta);

    core::CommandBus& m_bus;
    QWidget* m_window;
    CommandPaletteModel* m_model;
    QLineEdit* m_search;
    QListView* m_list;
};

}  // namespace mikserstudio::ui
