// Loading a plugin's QML panel, and showing the load errors in the panel instead.
#include "PluginPanelHost.h"

#include "Logging.h"
#include "PluginPanelBridge.h"

#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/ui/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

PluginPanelHost::PluginPanelHost(core::CommandBus& bus, const core::PluginManager& plugins, const QString& pluginId,
                                 const QString& pluginDirectory, const core::PanelContribution& panel, QWidget* parent)
    : QWidget(parent)
    , m_quick(new QQuickWidget(this))
    , m_bridge(new PluginPanelBridge(bus, plugins, pluginId, *m_quick->engine(), this))
    , m_error(new QLabel(this))
{
    setProperty("role", u"panel"_s);
    setAttribute(Qt::WA_StyledBackground);
    m_error->setProperty("role", u"error"_s);
    m_error->setWordWrap(true);
    m_error->setAlignment(Qt::AlignCenter);
    m_error->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_error->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_quick, 1);
    layout->addWidget(m_error, 1);

    m_quick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quick->setClearColor(darkTheme().panel);
    m_quick->rootContext()->setContextProperty(u"mikserstudio"_s, m_bridge);

    const QString path = QDir(pluginDirectory).filePath(panel.qml);
    if (!QFileInfo::exists(path)) {
        showError(u"The panel file %1 of %2 is missing. Reinstall the plugin."_s.arg(QDir::toNativeSeparators(path),
                                                                                     pluginId));
        return;
    }
    connect(m_quick, &QQuickWidget::statusChanged, this, [this, pluginId](QQuickWidget::Status status) {
        if (status != QQuickWidget::Error)
            return;
        QStringList messages;
        for (const QQmlError& error : m_quick->errors())
            messages.append(error.toString());
        qCWarning(lcPanels).noquote() << "Panel of plugin" << pluginId << "failed to load:" << messages.join(u"; "_s);
        showError(u"The panel could not be loaded:\n%1"_s.arg(messages.join(u'\n')));
    });
    m_quick->setSource(QUrl::fromLocalFile(path));
}

void PluginPanelHost::showError(const QString& message)
{
    m_quick->hide();
    m_error->setText(message);
    m_error->show();
}

}  // namespace mikserstudio::ui
