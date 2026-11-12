#include "mikserstudio/core/PluginManager.h"

#include "Logging.h"
#include "NativePlugin.h"
#include "PluginSettingsStore.h"
#include "mikserstudio/core/JsonSchema.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSet>

#include <map>
#include <ranges>
#include <tuple>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

struct Record {
    PluginState state;
    bool manifestValid = false;
    QJsonObject storedSettings;  // As saved, without defaults.
    std::unique_ptr<NativePlugin> native;
};

QString displayName(const Record& record)
{
    return record.state.manifest.name.isEmpty() ? record.state.manifest.id : record.state.manifest.name;
}

}  // namespace

struct PluginManager::Impl {
    Impl(PluginManager& owner, CommandBus& commandBus, const QString& settingsDirectory)
        : q(owner)
        , bus(commandBus)
        , store(settingsDirectory)
    {
    }

    Record* find(const QString& id)
    {
        const auto it = records.find(id);
        return it == records.end() ? nullptr : &it->second;
    }

    Record readRecord(const QString& directory) const;
    void loadStoredSettings(Record& record) const;
    void fail(const QString& id, const QString& message);

    PluginManager& q;
    CommandBus& bus;
    PluginSettingsStore store;
    QStringList searchPaths;
    QSet<QString> disabled;
    std::map<QString, Record> records;
};

Record PluginManager::Impl::readRecord(const QString& directory) const
{
    Record record;
    record.state.manifest.directory = QDir::fromNativeSeparators(QDir(directory).absolutePath());
    record.state.manifest.id = QDir(directory).dirName();
    record.state.status = PluginStatus::Failed;

    const QString manifestPath = QDir(directory).filePath(QLatin1StringView(kPluginManifestFileName));
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        record.state.error = u"Cannot read %1: %2"_s.arg(QDir::toNativeSeparators(manifestPath), file.errorString());
        return record;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        record.state.error = u"The plugin manifest %1 is not a valid JSON object (%2). Fix or reinstall the plugin."_s
                                 .arg(QDir::toNativeSeparators(manifestPath), parseError.errorString());
        return record;
    }
    // A broken manifest keeps the folder name as its id: its "id" is unvalidated and must not
    // claim (and so hide) the id of a valid plugin.
    auto manifest = parsePluginManifest(document.object(), directory);
    if (!manifest) {
        record.state.error = u"The plugin manifest %1 is invalid: %2. Fix or reinstall the plugin."_s.arg(
            QDir::toNativeSeparators(manifestPath), manifest.error());
        return record;
    }
    record.state.manifest = std::move(*manifest);
    record.state.status = disabled.contains(record.state.manifest.id) ? PluginStatus::Disabled
                                                                      : PluginStatus::Discovered;
    record.manifestValid = true;
    loadStoredSettings(record);
    return record;
}

void PluginManager::Impl::loadStoredSettings(Record& record) const
{
    const PluginManifest& manifest = record.state.manifest;
    auto stored = store.load(manifest.id);
    if (!stored) {
        qCWarning(lcPlugins).noquote() << "Using default settings for" << manifest.id << "-" << stored.error();
        return;
    }
    const QStringList problems = JsonSchema::validate(JsonSchema::applyDefaults(*stored, manifest.settingsSchema),
                                                      manifest.settingsSchema);
    if (!problems.isEmpty()) {
        qCWarning(lcPlugins).noquote() << "Ignoring stored settings of" << manifest.id
                                       << "because they do not match its settings schema:" << problems.join(u"; "_s);
        return;
    }
    record.storedSettings = *stored;
}

void PluginManager::Impl::fail(const QString& id, const QString& message)
{
    if (Record* record = find(id)) {
        record->state.status = PluginStatus::Failed;
        record->state.error = message;
    }
    qCWarning(lcPlugins).noquote() << "Plugin" << id << "failed:" << message;
    Q_EMIT q.pluginFailed(id, message);
    Q_EMIT q.pluginsChanged();
}

PluginManager::PluginManager(CommandBus& bus, QString settingsDirectory, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>(*this, bus, settingsDirectory))
{
    auto disabled = d->store.loadDisabled();
    if (disabled)
        d->disabled = std::move(*disabled);
    else
        qCWarning(lcPlugins).noquote() << "All plugins are enabled because the plugin list could not be read:"
                                       << disabled.error();
}

PluginManager::~PluginManager()
{
    for (auto& [id, record] : std::views::reverse(d->records))
        record.native.reset();
}

void PluginManager::addSearchPath(const QString& directory)
{
    const QString path = QDir::fromNativeSeparators(QDir(directory).absolutePath());
    if (!d->searchPaths.contains(path))
        d->searchPaths.append(path);
}

void PluginManager::discover()
{
    std::map<QString, Record> found;
    QList<std::pair<QString, QString>> failures;
    for (const QString& path : std::as_const(d->searchPaths)) {
        const QDir root(path);
        if (!root.exists()) {
            qCInfo(lcPlugins).noquote() << "Plugin folder" << QDir::toNativeSeparators(path) << "does not exist";
            continue;
        }
        for (const QFileInfo& folder : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!QFile::exists(QDir(folder.absoluteFilePath()).filePath(QLatin1StringView(kPluginManifestFileName))))
                continue;
            Record record = d->readRecord(folder.absoluteFilePath());
            const QString id = record.state.manifest.id;
            if (const auto existing = found.find(id); existing != found.end()) {
                // Broken manifests are listed under their folder name; a folder named like a plugin
                // id must not hide that plugin, so a valid manifest always wins the id.
                if (!record.manifestValid || !existing->second.manifestValid) {
                    const Record& broken = record.manifestValid ? existing->second : record;
                    qCWarning(lcPlugins).noquote()
                        << "Plugin folder" << QDir::toNativeSeparators(broken.state.manifest.directory)
                        << "is not listed because another plugin uses the id" << id << "-" << broken.state.error;
                    if (record.manifestValid) {
                        failures.removeIf([&id](const auto& failure) { return failure.first == id; });
                        existing->second = std::move(record);
                    }
                    continue;
                }
                failures.append(
                    {id, u"Plugin \"%1\" in %2 is ignored because %3 already provides a plugin with that id."_s.arg(
                             id, QDir::toNativeSeparators(record.state.manifest.directory),
                             QDir::toNativeSeparators(existing->second.state.manifest.directory))});
                continue;
            }
            if (record.state.status == PluginStatus::Failed)
                failures.append({id, record.state.error});
            found.emplace(id, std::move(record));
        }
    }
    for (auto& [id, record] : d->records) {
        if (record.native)
            found.insert_or_assign(id, std::move(record));
    }
    d->records = std::move(found);
    qCInfo(lcPlugins) << "Discovered" << d->records.size() << "plugins";

    for (const auto& [id, message] : std::as_const(failures)) {
        qCWarning(lcPlugins).noquote() << message;
        Q_EMIT pluginFailed(id, message);
    }
    Q_EMIT pluginsChanged();
}

QList<PluginState> PluginManager::plugins() const
{
    QList<PluginState> result;
    for (const auto& [id, record] : d->records)
        result.append(record.state);
    return result;
}

const PluginState* PluginManager::plugin(const QString& id) const
{
    const auto it = d->records.find(id);
    return it == d->records.end() ? nullptr : &it->second.state;
}

std::expected<void, QString> PluginManager::activate(const QString& id)
{
    Record* record = d->find(id);
    if (!record)
        return std::unexpected(u"There is no plugin \"%1\". Check the plugin folders and rescan."_s.arg(id));
    if (record->native)
        return {};
    if (!record->manifestValid)
        return std::unexpected(record->state.error);
    if (record->state.status == PluginStatus::Disabled)
        return std::unexpected(
            u"\"%1\" is disabled. Enable it in the plugin manager first."_s.arg(displayName(*record)));
    if (record->state.manifest.kind != u"native")
        return std::unexpected(
            u"\"%1\" is a Python plugin; the AI services sidecar runs it."_s.arg(displayName(*record)));

    auto native = NativePlugin::activate(record->state.manifest, d->bus, [this, id] { return settings(id); });
    record = d->find(id);  // The plugin may have run commands that changed the plugin list.
    if (!native) {
        d->fail(id, native.error());
        return std::unexpected(native.error());
    }
    if (!record)
        return std::unexpected(u"Plugin \"%1\" was removed while it started."_s.arg(id));
    record->native = std::move(*native);
    record->state.status = PluginStatus::Active;
    record->state.error.clear();
    Q_EMIT pluginsChanged();
    return {};
}

void PluginManager::deactivate(const QString& id)
{
    Record* record = d->find(id);
    if (!record || !record->native)
        return;
    std::unique_ptr<NativePlugin> native = std::move(record->native);
    record->state.status = d->disabled.contains(id) ? PluginStatus::Disabled : PluginStatus::Discovered;
    native.reset();
    qCInfo(lcPlugins).noquote() << "Deactivated" << id;
    Q_EMIT pluginsChanged();
}

void PluginManager::activateAll()
{
    QStringList ids;
    for (const auto& [id, record] : d->records) {
        if (record.manifestValid && record.state.status == PluginStatus::Discovered &&
            record.state.manifest.kind == u"native")
            ids.append(id);
    }
    for (const QString& id : std::as_const(ids))
        std::ignore = activate(id);  // Failures are reported through pluginFailed().
}

std::expected<void, QString> PluginManager::setEnabled(const QString& id, bool enabled)
{
    Record* record = d->find(id);
    if (!record || !record->manifestValid)
        return std::unexpected(u"There is no usable plugin \"%1\"."_s.arg(id));

    QSet<QString> disabled = d->disabled;
    if (enabled)
        disabled.remove(id);
    else
        disabled.insert(id);
    if (auto saved = d->store.saveDisabled(disabled); !saved)
        return std::unexpected(saved.error());
    d->disabled = std::move(disabled);

    if (enabled) {
        if (record->state.status == PluginStatus::Disabled)
            record->state.status = PluginStatus::Discovered;
        Q_EMIT pluginsChanged();
        return {};
    }
    if (record->native)
        deactivate(id);
    if (Record* current = d->find(id))
        current->state.status = PluginStatus::Disabled;
    Q_EMIT pluginsChanged();
    return {};
}

QJsonObject PluginManager::settings(const QString& id) const
{
    const auto it = d->records.find(id);
    if (it == d->records.end())
        return {};
    return JsonSchema::applyDefaults(it->second.storedSettings, it->second.state.manifest.settingsSchema);
}

std::expected<void, QString> PluginManager::setSettings(const QString& id, const QJsonObject& settings)
{
    Record* record = d->find(id);
    if (!record || !record->manifestValid)
        return std::unexpected(u"There is no usable plugin \"%1\"."_s.arg(id));
    const QJsonObject& schema = record->state.manifest.settingsSchema;
    if (schema.isEmpty() && !settings.isEmpty())
        return std::unexpected(u"\"%1\" has no settings."_s.arg(displayName(*record)));

    const QJsonObject effective = JsonSchema::applyDefaults(settings, schema);
    if (const QStringList problems = JsonSchema::validate(effective, schema); !problems.isEmpty())
        return std::unexpected(
            u"The settings for \"%1\" are not valid: %2"_s.arg(displayName(*record), problems.join(u"; "_s)));
    if (auto saved = d->store.save(id, settings); !saved)
        return std::unexpected(saved.error());

    record->storedSettings = settings;
    Q_EMIT settingsChanged(id, effective);
    return {};
}

}  // namespace mikserstudio::core
