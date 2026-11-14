// The Settings window: one page per section, with a form per built-in plugin, saved through
// plugins.set_settings.
#include "SettingsWindow.h"

#include "CommandInvoker.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/ui/SchemaForm.h"
#include "mikserstudio/ui/Theme.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

SettingsWindow::SettingsWindow(core::CommandBus& bus, core::PluginManager& plugins, QWidget* parent)
    : QDialog(parent)
    , m_bus(bus)
    , m_plugins(plugins)
    , m_sections(new QListWidget(this))
    , m_pages(new QStackedWidget(this))
{
    setWindowTitle(u"Settings"_s);
    resize(720, 480);

    m_sections->setFixedWidth(170);
    connect(m_sections, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    body->addWidget(m_sections);
    body->addWidget(m_pages, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);
    layout->addLayout(body, 1);
    layout->addWidget(buttons);

    build();
}

void SettingsWindow::showSection(const QString& section)
{
    const auto found = m_byName.find(section);
    if (found != m_byName.end())
        m_sections->setCurrentRow(m_pages->indexOf(found->second.page));
}

void SettingsWindow::build()
{
    for (const core::PluginState& state : m_plugins.plugins()) {
        const core::PluginManifest& manifest = state.manifest;
        if (state.source != core::PluginSource::BuiltIn || manifest.settingsSection.isEmpty() ||
            manifest.settingsSchema.value(u"properties").toObject().isEmpty())
            continue;
        auto [it, added] = m_byName.try_emplace(manifest.settingsSection);
        Section& section = it->second;
        if (added) {
            auto* content = new QWidget;
            auto* heading = new QLabel(manifest.settingsSection, content);
            heading->setProperty("role", u"heading"_s);
            section.groups = new QVBoxLayout;
            section.groups->setSpacing(12);
            section.feedback = new QLabel(content);
            section.feedback->setWordWrap(true);
            auto* save = new QPushButton(u"Save"_s, content);
            save->setProperty("role", u"primary"_s);
            save->setToolTip(u"Saves the settings of this section (plugins.set_settings)."_s);
            const QString name = manifest.settingsSection;
            connect(save, &QPushButton::clicked, this, [this, name] { SettingsWindow::save(name); });

            auto* footer = new QHBoxLayout;
            footer->addWidget(section.feedback, 1);
            footer->addWidget(save);
            auto* layout = new QVBoxLayout(content);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(12);
            layout->addWidget(heading);
            layout->addLayout(section.groups);
            layout->addStretch();
            layout->addLayout(footer);

            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(content);
            m_pages->addWidget(scroll);
            section.page = scroll;
            m_sections->addItem(name);
        }

        auto* group = new QGroupBox(manifest.name);
        group->setToolTip(manifest.description);
        auto* form = new SchemaForm(manifest.settingsSchema, m_plugins.settings(manifest.id), group);
        auto* groupLayout = new QVBoxLayout(group);
        groupLayout->addWidget(form);
        section.groups->addWidget(group);
        section.forms.emplace(manifest.id, form);
    }
    if (m_sections->count() > 0)
        m_sections->setCurrentRow(0);
}

void SettingsWindow::save(const QString& name)
{
    Section& section = m_byName.at(name);
    const auto show = [&section](const QString& message, bool error) {
        section.feedback->setText(message);
        section.feedback->setStyleSheet(
            u"color: %1;"_s.arg((error ? darkTheme().danger : darkTheme().textMuted).name()));
    };
    bool valid = true;
    for (const auto& [id, form] : section.forms)
        valid = form->validate().isEmpty() && valid;
    if (!valid) {
        show(u"Fix the highlighted settings first."_s, true);
        return;
    }
    for (const auto& [id, form] : section.forms) {
        const auto result = executeAsking(m_bus, u"plugins.set_settings"_s,
                                          {{u"id"_s, id}, {u"settings"_s, form->value()}}, this);
        if (!result)
            return;  // The person did not agree.
        if (!*result) {
            const core::PluginState* state = m_plugins.plugin(id);
            show(u"%1: %2"_s.arg(state ? state->manifest.name : id, result->error().message), true);
            return;
        }
    }
    show(u"Settings saved. Jobs asked for from now on use them."_s, false);
}

}  // namespace mikserstudio::ui
