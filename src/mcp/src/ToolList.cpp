// Turning command specs into MCP tools.
#include "ToolList.h"

#include "Logging.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mikserstudio::mcp {

namespace {

constexpr qsizetype kMaxNameLength = 64;

QString textOr(const QJsonObject& command, const QString& key, const QString& fallback)
{
    const QString text = command.value(key).toString();
    return text.isEmpty() ? fallback : text;
}

}  // namespace

QString toolName(const QString& commandId)
{
    static const QRegularExpression notInNames(u"[^A-Za-z0-9_-]"_s);
    return QString(commandId).replace(notInNames, u"_"_s).left(kMaxNameLength);
}

QJsonObject toolFor(const QJsonObject& command)
{
    const QString id = command.value(u"id"_s).toString();
    const QString title = textOr(command, u"title"_s, id);
    QString description = textOr(command, u"description"_s, title);
    if (command.value(u"undoable"_s).toBool())
        description += u" It can be undone (edit_undo)."_s;
    const QString confirmation = command.value(u"confirmation"_s).toString();
    if (!confirmation.isEmpty())
        description += u" The person is asked before it runs: “%1”"_s.arg(confirmation);
    QJsonObject schema = command.value(u"paramsSchema"_s).toObject();
    if (schema.value(u"type"_s) != u"object"_s) {
        schema = QJsonObject{
            {u"type"_s, u"object"_s}, {u"properties"_s, QJsonObject{}}, {u"additionalProperties"_s, false}};
    }
    const bool readOnly = command.value(u"readOnly"_s).toBool();
    return QJsonObject{
        {u"name"_s, toolName(id)},
        {u"title"_s, title},
        {u"description"_s, description},
        {u"inputSchema"_s, schema},
        {u"annotations"_s,
         QJsonObject{
             {u"title"_s, title},
             {u"readOnlyHint"_s, readOnly},
             {u"destructiveHint"_s, !confirmation.isEmpty() && !readOnly},
             {u"openWorldHint"_s, false},
         }},
    };
}

Tools toolsFor(const QJsonArray& commands, const QStringList& taken)
{
    Tools tools;
    for (const QJsonValue listed : commands) {
        const QJsonObject command = listed.toObject();
        if (!command.value(u"id"_s).isString())
            continue;
        const QJsonObject tool = toolFor(command);
        const QString name = tool.value(u"name"_s).toString();
        if (taken.contains(name) || tools.commands.contains(name)) {
            qCWarning(lcMcp).noquote() << u"Left out %1: its tool name %2 is taken."_s.arg(
                command.value(u"id"_s).toString(), name);
            continue;
        }
        tools.commands.insert(name, command);
        tools.tools.append(tool);
    }
    return tools;
}

}  // namespace mikserstudio::mcp
