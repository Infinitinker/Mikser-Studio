// The Plugins panel: lists the plugins with their status, switches them on and off and edits their
// settings.
#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedLayout;

namespace mikserstudio::core {
class PluginManager;
}

namespace mikserstudio::ui {

class SchemaForm;

// "Plugins" panel: every discovered plugin with its status and error; the selected one can be
// activated, deactivated, enabled or disabled, and its settings edited in a form generated from
// its settingsSchema (saved with PluginManager::setSettings).
class PluginsPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PluginsPanel(core::PluginManager& plugins, QWidget* parent = nullptr);

    void selectPlugin(const QString& id);
    [[nodiscard]] QString selectedPlugin() const { return m_selectedId; }
    [[nodiscard]] SchemaForm* settingsForm() const { return m_form; }
    [[nodiscard]] QString feedbackText() const;
    [[nodiscard]] int pluginCount() const;

    void toggleActive();
    void saveSettings();

private:
    void reloadList();
    void showDetails();
    void setFeedback(const QString& message, bool error);

    core::PluginManager& m_plugins;
    QString m_selectedId;
    QString m_formPluginId;  // Whose settings the form shows.
    QListWidget* m_list = nullptr;
    QStackedLayout* m_stack = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_meta = nullptr;
    QLabel* m_description = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_error = nullptr;
    QPushButton* m_activate = nullptr;
    QCheckBox* m_enabled = nullptr;
    QLabel* m_settingsHeading = nullptr;
    SchemaForm* m_form = nullptr;
    QPushButton* m_save = nullptr;
    QLabel* m_feedback = nullptr;
};

}  // namespace mikserstudio::ui
