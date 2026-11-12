#include "CommandText.h"

#include "mikserstudio/core/CommandBus.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString elide(const QString& text, qsizetype maxChars)
{
    return text.size() > maxChars ? text.left(maxChars - 1) + u"…"_s : text;
}

}  // namespace

QString commandTitle(const core::CommandBus& bus, const QString& id)
{
    const core::CommandSpec* spec = bus.spec(id);
    return spec ? spec->title : id;
}

QString compactJson(const QJsonObject& object, qsizetype maxChars)
{
    if (object.isEmpty())
        return {};
    return elide(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)), maxChars);
}

QString resultSummary(const QJsonObject& result, qsizetype maxChars)
{
    for (const auto* const key : {u"text", u"message"}) {
        const QString text = result.value(QStringView(key)).toString();
        if (!text.isEmpty())
            return elide(text, maxChars);
    }
    const QString json = compactJson(result, maxChars);
    return json.isEmpty() ? u"Done"_s : json;
}

}  // namespace mikserstudio::ui
