#include "mikserstudio/core/PluginManager.h"

#include "mikserstudio/core/JsonSchema.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

const QJsonObject& manifestSchema()
{
    static const QJsonObject schema = QJsonDocument::fromJson(R"json({
        "type": "object",
        "required": ["id", "name", "version", "apiVersion", "kind", "entry"],
        "properties": {
            "id": {"type": "string", "pattern": "^[a-z0-9_]+(\\.[a-z0-9_]+)+$"},
            "name": {"type": "string", "minLength": 1},
            "version": {
                "type": "string",
                "pattern": "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$"
            },
            "description": {"type": "string", "default": ""},
            "apiVersion": {"type": "integer", "minimum": 1},
            "kind": {"enum": ["native", "python"]},
            "entry": {"type": "string", "minLength": 1},
            "capabilities": {"type": "array", "items": {"type": "string", "minLength": 1}, "default": []},
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

std::expected<void, QString> checkEntry(const QString& kind, const QString& entry)
{
    if (kind == u"native") {
        static const QRegularExpression libraryName(u"^[A-Za-z0-9_][A-Za-z0-9_.-]*$"_s);
        if (!libraryName.match(entry).hasMatch() || entry.contains(u".."))
            return std::unexpected(
                u"/entry: must be the library file name without folder or extension, e.g. \"hello\""_s);
        return {};
    }
    static const QRegularExpression pythonEntry(u"^[A-Za-z_][A-Za-z0-9_.]*:[A-Za-z_][A-Za-z0-9_]*$"_s);
    if (!pythonEntry.match(entry).hasMatch())
        return std::unexpected(u"/entry: must be \"module:attribute\" for Python plugins, e.g. \"my_plugin:Plugin\""_s);
    return {};
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
            return std::unexpected(pointer + u"/id: panel id \"%1\" is used twice"_s.arg(contribution.id));
        if (!QDir::isRelativePath(contribution.qml) || contribution.qml.split(u'/').contains(u".."_s) ||
            contribution.qml.contains(u'\\') || contribution.qml.contains(u':')) {
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
    const QString kind = manifest.value(u"kind").toString();
    const QString entry = manifest.value(u"entry").toString();
    if (auto entryOk = checkEntry(kind, entry); !entryOk)
        return std::unexpected(entryOk.error());

    const QJsonObject settingsSchema = manifest.value(u"settingsSchema").toObject();
    if (!settingsSchema.isEmpty() && settingsSchema.value(u"type") != u"object"_s)
        return std::unexpected(u"/settingsSchema: must have \"type\": \"object\" because settings are a JSON object"_s);

    auto panels = parsePanels(manifest.value(u"panels").toArray());
    if (!panels)
        return std::unexpected(panels.error());

    QStringList capabilities;
    for (const QJsonValue capability : manifest.value(u"capabilities").toArray())
        capabilities.append(capability.toString());

    return PluginManifest{
        .id = manifest.value(u"id").toString(),
        .name = manifest.value(u"name").toString(),
        .version = manifest.value(u"version").toString(),
        .description = manifest.value(u"description").toString(),
        .apiVersion = apiVersion,
        .kind = kind,
        .entry = entry,
        .capabilities = capabilities,
        .settingsSchema = settingsSchema,
        .panels = std::move(*panels),
        .directory = QDir::fromNativeSeparators(QDir(directory).absolutePath()),
    };
}

}  // namespace mikserstudio::core
