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

// Whether a client gets results as structured content too (MCP 2025-06-18 and later), and so the
// tools' output schemas.
enum class Results { TextOnly, Structured };

// The MCP tool that runs `command`, a command spec as GET /v1/commands lists it. Its outputSchema
// is the command's resultSchema, given only to clients that get structured results.
QJsonObject toolFor(const QJsonObject& command, Results results);

// The tools for a list of commands, and the command each tool name runs.
struct Tools {
    QJsonArray tools;
    QHash<QString, QJsonObject> commands;
};

// The tools for `commands`. A command whose tool name is in `taken`, the bridge's own tools, or that an
// earlier command in the list already has, is left out.
Tools toolsFor(const QJsonArray& commands, const QStringList& taken, Results results);

}  // namespace mikserstudio::mcp
