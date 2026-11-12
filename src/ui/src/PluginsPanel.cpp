// The Plugins panel: plugin list, activation and enabling, and the settings form from the settings schema.
#include "PluginsPanel.h"

#include "PanelLayout.h"

#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/ui/SchemaForm.h"
#include "mikserstudio/ui/Theme.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString statusText(const core::PluginState& state)
{
    switch (state.status) {
    case core::PluginStatus::Active:
        return u"Active"_s;
    case core::PluginStatus::Failed:
        return u"Failed"_s;
    case core::PluginStatus::Disabled:
        return u"Disabled"_s;
    case core::PluginStatus::Discovered:
        break;
    }
    return state.manifest.kind == u"python" ? u"Runs in the AI service"_s : u"Not active"_s;
}

QColor statusColor(core::PluginStatus status)
{
    const ThemeTokens& theme = darkTheme();
    switch (status) {
    case core::PluginStatus::Active:
        return theme.success;
    case core::PluginStatus::Failed:
        return theme.danger;
    case core::PluginStatus::Disabled:
        return theme.textDisabled;
    case core::PluginStatus::Discovered:
        break;
    }
    return theme.textMuted;
}

}  // namespace

PluginsPanel::PluginsPanel(core::PluginManager& plugins, QWidget* parent)
    : QWidget(parent)
    , m_plugins(plugins)
{
    auto* layout = makePanelLayout(this);
    auto* rescan = new QPushButton(u"Rescan"_s, this);
    rescan->setToolTip(u"Reads the plugin folders again for new or changed plugins."_s);
    connect(rescan, &QPushButton::clicked, this, [this] { m_plugins.discover(); });
    layout->addLayout(makePanelHeader(u"Plugins"_s, {rescan}));

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    m_list = new QListWidget(splitter);
    m_list->setIconSize(QSize(8, 8));
    m_list->setMinimumHeight(80);
    splitter->addWidget(m_list);

    auto* detailsHost = new QWidget(splitter);
    m_stack = new QStackedLayout(detailsHost);
    auto* empty = makeLabel(u"No plugins were found."_s, u"muted"_s);
    empty->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(empty);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* details = new QWidget;
    details->setProperty("role", u"panel"_s);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(0, 8, 0, 0);
    detailsLayout->setSpacing(6);
    m_name = makeLabel({}, u"heading"_s, details);
    m_meta = makeLabel({}, u"hint"_s, details);
    m_meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_description = makeLabel({}, u"muted"_s, details);
    m_description->setWordWrap(true);
    m_status = new QLabel(details);
    m_error = makeLabel({}, u"error"_s, details);
    m_error->setWordWrap(true);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* actions = new QHBoxLayout;
    m_activate = new QPushButton(details);
    m_enabled = new QCheckBox(u"Enabled"_s, details);
    m_enabled->setToolTip(u"Disabled plugins are never activated, also not at the next start."_s);
    actions->addWidget(m_activate);
    actions->addWidget(m_enabled);
    actions->addStretch();

    m_settingsHeading = makeLabel(u"Settings"_s, u"heading"_s, details);
    m_form = new SchemaForm(details);
    auto* saveRow = new QHBoxLayout;
    m_save = new QPushButton(u"Save settings"_s, details);
    m_save->setProperty("role", u"primary"_s);
    m_feedback = new QLabel(details);
    m_feedback->setWordWrap(true);
    saveRow->addWidget(m_feedback, 1);
    saveRow->addWidget(m_save);

    detailsLayout->addWidget(m_name);
    detailsLayout->addWidget(m_meta);
    detailsLayout->addWidget(m_description);
    detailsLayout->addWidget(m_status);
    detailsLayout->addWidget(m_error);
    detailsLayout->addLayout(actions);
    detailsLayout->addSpacing(8);
    detailsLayout->addWidget(m_settingsHeading);
    detailsLayout->addWidget(m_form);
    detailsLayout->addLayout(saveRow);
    detailsLayout->addStretch();
    scroll->setWidget(details);
    m_stack->addWidget(scroll);
    splitter->addWidget(detailsHost);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        m_selectedId = item ? item->data(Qt::UserRole).toString() : QString();
        setFeedback({}, false);
        showDetails();
    });
    connect(m_activate, &QPushButton::clicked, this, &PluginsPanel::toggleActive);
    connect(m_enabled, &QCheckBox::toggled, this, [this](bool enabled) {
        if (const auto result = m_plugins.setEnabled(m_selectedId, enabled); !result)
            setFeedback(result.error(), true);
    });
    connect(m_save, &QPushButton::clicked, this, &PluginsPanel::saveSettings);
    connect(&plugins, &core::PluginManager::pluginsChanged, this, &PluginsPanel::reloadList);
    connect(&plugins, &core::PluginManager::settingsChanged, this, [this](const QString& id) {
        if (id == m_selectedId)
            m_form->setValue(m_plugins.settings(id));
    });
    reloadList();
}

void PluginsPanel::selectPlugin(const QString& id)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(row);
            return;
        }
    }
}

void PluginsPanel::toggleActive()
{
    const core::PluginState* state = m_plugins.plugin(m_selectedId);
    if (!state)
        return;
    if (state->status == core::PluginStatus::Active) {
        m_plugins.deactivate(m_selectedId);
        setFeedback(u"Deactivated."_s, false);
    } else if (const auto result = m_plugins.activate(m_selectedId); !result) {
        setFeedback(result.error(), true);
    } else {
        setFeedback(u"Activated."_s, false);
    }
}

void PluginsPanel::saveSettings()
{
    if (!m_form->validate().isEmpty()) {
        setFeedback(u"Fix the highlighted settings first."_s, true);
        return;
    }
    if (const auto result = m_plugins.setSettings(m_selectedId, m_form->value()); !result) {
        setFeedback(result.error(), true);
        return;
    }
    setFeedback(u"Settings saved."_s, false);
}

void PluginsPanel::reloadList()
{
    const QString keep = m_selectedId;
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const core::PluginState& state : m_plugins.plugins()) {
        auto* item = new QListWidgetItem(u"%1  %2"_s.arg(state.manifest.name, state.manifest.version), m_list);
        item->setData(Qt::UserRole, state.manifest.id);
        item->setData(Qt::DecorationRole, statusColor(state.status));
        item->setToolTip(u"%1 — %2"_s.arg(state.manifest.id, statusText(state)));
        if (state.manifest.id == keep)
            m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
    m_selectedId = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
    showDetails();
}

void PluginsPanel::showDetails()
{
    const core::PluginState* state = m_plugins.plugin(m_selectedId);
    if (!state) {
        m_stack->setCurrentIndex(0);
        return;
    }
    const core::PluginManifest& manifest = state->manifest;
    m_name->setText(manifest.name);
    m_meta->setText(u"%1  ·  %2  ·  %3"_s.arg(manifest.id, manifest.version, manifest.kind));
    m_description->setText(manifest.description);
    m_description->setVisible(!manifest.description.isEmpty());
    m_status->setText(statusText(*state));
    m_status->setStyleSheet(u"color: %1; font-weight: 600;"_s.arg(statusColor(state->status).name()));
    m_error->setText(state->error);
    m_error->setVisible(!state->error.isEmpty());

    const bool native = manifest.kind == u"native";
    const bool active = state->status == core::PluginStatus::Active;
    m_activate->setText(active ? u"Deactivate"_s : u"Activate"_s);
    m_activate->setEnabled(native && state->status != core::PluginStatus::Disabled);
    {
        const QSignalBlocker blocker(m_enabled);
        m_enabled->setChecked(state->status != core::PluginStatus::Disabled);
    }

    const bool hasSettings = !manifest.settingsSchema.value(u"properties").toObject().isEmpty();
    if (m_formPluginId != manifest.id || m_form->schema() != manifest.settingsSchema) {
        m_form->setSchema(manifest.settingsSchema, m_plugins.settings(manifest.id));
        m_formPluginId = manifest.id;
    }
    m_settingsHeading->setVisible(hasSettings);
    m_form->setVisible(hasSettings);
    m_save->setVisible(hasSettings);
    m_stack->setCurrentIndex(1);
}

void PluginsPanel::setFeedback(const QString& message, bool error)
{
    m_feedback->setText(message);
    m_feedback->setStyleSheet(u"color: %1;"_s.arg((error ? darkTheme().danger : darkTheme().textMuted).name()));
}

}  // namespace mikserstudio::ui
