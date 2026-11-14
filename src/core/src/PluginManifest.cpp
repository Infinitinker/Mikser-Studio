// Reading and validating a plugin's mikserstudio-plugin.json manifest.
#include "mikserstudio/core/PluginManager.h"

#include "mikserstudio/core/Command.h"
#include "mikserstudio/core/JsonSchema.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

const QJsonObject& manifestSchema()
{
    static const QJsonObject schema = QJsonDocument::fromJson(R"json({
        "type": "object",
        "required": ["id", "name", "version", "apiVersion", "entry"],
        "properties": {
            "id": {"type": "string", "pattern": "^[a-z0-9_]+(\\.[a-z0-9_]+)+$"},
            "name": {"type": "string", "minLength": 1},
            "version": {
                "type": "string",
                "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$"
            },
            "description": {"type": "string", "default": ""},
            "apiVersion": {"type": "integer", "minimum": 1},
            "entry": {"type": "string", "minLength": 1},
            "permissions": {"type": "array", "items": {"type": "string"}, "default": []},
            "settingsSchema": {"type": "object"},
            "panels": {
                "type": "array",
                "default": [],
                "items": {
                    "type": "object",
                    "required": ["id", "title", "qml"],
                    "additionalProperties": false,
                    "properties": {
                        "id": {"type": "string", "pattern": "^[a-z0-9_][a-z0-9_.-]*$"},
                        "title": {"type": "string", "minLength": 1},
                        "qml": {"type": "string", "minLength": 1},
                        "defaultArea": {"enum": ["left", "right", "bottom", "center", "floating"]}
                    }
                }
            }
        }
    })json")
                                          .object();
    return schema;
}

// A path inside the plugin folder, with '/' between folders.
bool isInsidePath(const QString& path)
{
    return QDir::isRelativePath(path) && !path.split(u'/').contains(u".."_s) && !path.contains(u'\\') &&
           !path.contains(u':');
}

std::expected<void, QString> checkEntry(const QString& entry)
{
    if (!isInsidePath(entry) || !entry.endsWith(u".lua"_s))
        return std::unexpected(u"/entry: must be a Lua file inside the plugin folder using '/', e.g. \"main.lua\""_s);
    return {};
}

std::expected<QStringList, QString> parsePermissions(const QJsonArray& permissions)
{
    QStringList result;
    for (qsizetype i = 0; i < permissions.size(); ++i) {
        const QString permission = permissions.at(i).toString();
        if (!allPermissions().contains(permission)) {
            return std::unexpected(u"/permissions/%1: “%2” is not a permission; use one of %3"_s.arg(i).arg(
                permission, allPermissions().join(u", "_s)));
        }
        if (result.contains(permission))
            return std::unexpected(u"/permissions/%1: “%2” is listed twice"_s.arg(i).arg(permission));
        result.append(permission);
    }
    return result;
}

std::expected<QList<PanelContribution>, QString> parsePanels(const QJsonArray& panels)
{
    QList<PanelContribution> result;
    QSet<QString> ids;
    for (qsizetype i = 0; i < panels.size(); ++i) {
        const QJsonObject panel = panels.at(i).toObject();
        PanelContribution contribution{
            .id = panel.value(u"id").toString(),
            .title = panel.value(u"title").toString(),
            .qml = panel.value(u"qml").toString(),
            .defaultArea = panel.value(u"defaultArea").toString(u"right"_s),
        };
        const QString pointer = u"/panels/%1"_s.arg(i);
        if (ids.contains(contribution.id))
            return std::unexpected(pointer + u"/id: panel id “%1” is used twice"_s.arg(contribution.id));
        if (!isInsidePath(contribution.qml)) {
            return std::unexpected(pointer +
                                   u"/qml: must be a path inside the plugin folder using '/', e.g. \"panel.qml\""_s);
        }
        ids.insert(contribution.id);
        result.append(std::move(contribution));
    }
    return result;
}

}  // namespace

std::expected<PluginManifest, QString> parsePluginManifest(const QJsonObject& json, const QString& directory)
{
    // Native and Python plugins were once run; named apart so their authors learn why.
    if (json.contains(u"kind")) {
        return std::unexpected(u"/kind: Mikser Studio runs Lua plugins only; install a Lua version of the plugin, "
                               "whose manifest names no kind"_s);
    }
    if (json.contains(u"capabilities"))
        return std::unexpected(u"/capabilities: replaced by \"permissions\", the permissions the plugin needs"_s);
    const QJsonObject manifest = JsonSchema::applyDefaults(json, manifestSchema());
    if (const QStringList problems = JsonSchema::validate(manifest, manifestSchema()); !problems.isEmpty())
        return std::unexpected(problems.join(u"; "_s));

    const int apiVersion = manifest.value(u"apiVersion").toInt();
    if (apiVersion != kPluginApiVersion) {
        return std::unexpected(
            u"/apiVersion: the plugin needs plugin API version %1 but this version of Mikser Studio provides version %2; "
            "install a plugin build made for this Mikser Studio version"_s.arg(apiVersion)
                .arg(kPluginApiVersion));
    }
    const QString entry = manifest.value(u"entry").toString();
    if (auto entryOk = checkEntry(entry); !entryOk)
        return std::unexpected(entryOk.error());

    const QJsonObject settingsSchema = manifest.value(u"settingsSchema").toObject();
    if (!settingsSchema.isEmpty() && settingsSchema.value(u"type") != u"object"_s)
        return std::unexpected(u"/settingsSchema: must have \"type\": \"object\" because settings are a JSON object"_s);

    auto panels = parsePanels(manifest.value(u"panels").toArray());
    if (!panels)
        return std::unexpected(panels.error());

    auto permissions = parsePermissions(manifest.value(u"permissions").toArray());
    if (!permissions)
        return std::unexpected(permissions.error());

    return PluginManifest{
        .id = manifest.value(u"id").toString(),
        .name = manifest.value(u"name").toString(),
        .version = manifest.value(u"version").toString(),
        .description = manifest.value(u"description").toString(),
        .apiVersion = apiVersion,
        .entry = entry,
        .permissions = std::move(*permissions),
        .settingsSchema = settingsSchema,
        .panels = std::move(*panels),
        .directory = QDir::fromNativeSeparators(QDir(directory).absolutePath()),
    };
}

}  // namespace mikserstudio::core
