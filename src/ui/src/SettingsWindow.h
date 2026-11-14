// The Settings window: the settings of the built-in plugins, section by section.
#pragma once

#include <QDialog>
#include <QString>

#include <map>

class QLabel;
class QListWidget;
class QVBoxLayout;
class QStackedWidget;

namespace mikserstudio::core {
class CommandBus;
class PluginManager;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class SchemaForm;

// A list of sections on the left and the chosen one on the right. A section gathers the built-in
// plugins whose manifests name it as their `settingsSection`, such as "AI tools" for the Ollama and
// ComfyUI plugins: each plugin's settings are a form drawn from its settings schema. Saving a section
// runs plugins.set_settings for each of its plugins, so the change goes through the command bus and
// a plugin reads it the next time it runs. The person's own plugins keep their settings in the
// Plugins panel.
class SettingsWindow final : public QDialog {
    Q_OBJECT

public:
    SettingsWindow(core::CommandBus& bus, core::PluginManager& plugins, QWidget* parent = nullptr);

    // Shows the section named `section` (such as "AI tools"), if there is one.
    void showSection(const QString& section);

private:
    struct Section {
        QWidget* page = nullptr;  // In m_pages.
        QVBoxLayout* groups = nullptr;  // A group box per plugin.
        QLabel* feedback = nullptr;
        std::map<QString, SchemaForm*> forms;  // By plugin id.
    };

    void build();
    void save(const QString& name);  // The section of that name.

    core::CommandBus& m_bus;
    core::PluginManager& m_plugins;
    QListWidget* m_sections;
    QStackedWidget* m_pages;
    std::map<QString, Section> m_byName;
};

}  // namespace mikserstudio::ui
