#include "PluginPanelBridge.h"

#include "CommandInvoker.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"

#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

PluginPanelBridge::PluginPanelBridge(core::CommandBus& bus, const core::PluginManager& plugins, QString pluginId,
                                     QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_pluginId(std::move(pluginId))
    , m_settings(plugins.settings(m_pluginId).toVariantMap())
{
    connect(&plugins, &core::PluginManager::settingsChanged, this,
            [this](const QString& id, const QJsonObject& settings) {
                if (id != m_pluginId)
                    return;
                m_settings = settings.toVariantMap();
                Q_EMIT settingsChanged();
            });
}

QVariantMap PluginPanelBridge::execute(const QString& commandId, const QVariantMap& params)
{
    const auto run = executeInteractive(m_bus, commandId, QJsonObject::fromVariantMap(params), nullptr);
    if (!run) {
        return {
            {u"ok"_s, false},
            {u"error"_s, QVariantMap{{u"code"_s, u"cancelled"_s}, {u"message"_s, u"You cancelled the command."_s}}},
        };
    }
    const auto& result = *run;
    if (result)
        return {{u"ok"_s, true}, {u"result"_s, result->toVariantMap()}};
    return {
        {u"ok"_s, false},
        {u"error"_s, QVariantMap{{u"code"_s, core::toString(result.error().code)}, {u"message"_s, result.error().message}}},
    };
}

}  // namespace mikserstudio::ui
