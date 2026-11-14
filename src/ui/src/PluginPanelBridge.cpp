// The plugin panel bridge: runs commands for QML with confirmations and passes on live settings.
#include "PluginPanelBridge.h"

#include "CommandInvoker.h"
#include "Logging.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"

#include <QJSEngine>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QVariantMap reply(const InteractiveResult& run)
{
    if (!run) {
        return {
            {u"ok"_s, false},
            {u"error"_s, QVariantMap{{u"code"_s, u"canceled"_s}, {u"message"_s, u"You canceled the command."_s}}},
        };
    }
    const auto& result = *run;
    if (result)
        return {{u"ok"_s, true}, {u"result"_s, result->toVariantMap()}};
    return {
        {u"ok"_s, false},
        {u"error"_s,
         QVariantMap{{u"code"_s, core::toString(result.error().code)}, {u"message"_s, result.error().message}}},
    };
}

}  // namespace

PluginPanelBridge::PluginPanelBridge(core::CommandBus& bus, const core::PluginManager& plugins, QString pluginId,
                                     QJSEngine& engine, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_engine(engine)
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

void PluginPanelBridge::execute(const QString& commandId, const QVariantMap& params, const QJSValue& callback)
{
    if (!callback.isCallable()) {
        qCWarning(lcPanels).noquote() << "Plugin" << m_pluginId << "ran" << commandId
                                      << "without a callback: mikserstudio.execute(commandId, params, callback)";
    }
    executeInteractiveLater(m_bus, commandId, QJsonObject::fromVariantMap(params), nullptr,
                            [this, callback](const InteractiveResult& run) mutable {
                                if (callback.isCallable())
                                    callback.call({m_engine.toScriptValue(reply(run))});
                            });
}

}  // namespace mikserstudio::ui
