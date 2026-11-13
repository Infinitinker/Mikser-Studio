// Mikser Studio's commands as MCP tools: their names, descriptions, input schemas and hints.
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace mikserstudio::mcp {

// The tool name for a command, in the letters, digits, "_" and "-" that clients accept everywhere, at most
// 64 of them: project.import_media becomes project_import_media.
QString toolName(const QString& commandId);

// The MCP tool that runs `command`, a command spec as GET /v1/commands lists it.
QJsonObject toolFor(const QJsonObject& command);

// The tools for a list of commands, and the command each tool name runs.
struct Tools {
    QJsonArray tools;
    QHash<QString, QJsonObject> commands;
};

// The tools for `commands`. A command whose tool name is in `taken`, the bridge's own tools, or that an
// earlier command in the list already has, is left out.
Tools toolsFor(const QJsonArray& commands, const QStringList& taken);

}  // namespace mikserstudio::mcp
