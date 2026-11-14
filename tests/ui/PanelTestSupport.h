// Running commands from a test as a plugin's QML panel does: through mikserstudio.execute and a
// JavaScript callback.
#pragma once

#include "PluginPanelBridge.h"

#include <QJSEngine>
#include <QVariantMap>

namespace mikserstudio::ui::test {

// Calls bridge.execute(id, params, callback) with a JavaScript callback and returns the reply it
// got, or an empty map when none came before execute returned.
inline QVariantMap executeInPanel(PluginPanelBridge& bridge, QJSEngine& engine, const QString& id,
                                  const QVariantMap& params = {})
{
    QJSValue holder = engine.newObject();
    const QJSValue callback =
        engine.evaluate(QStringLiteral("(function (holder) { return function (reply) { holder.reply = reply; }; })"))
            .call({holder});
    bridge.execute(id, params, callback);
    return holder.property(QStringLiteral("reply")).toVariant().toMap();
}

}  // namespace mikserstudio::ui::test
