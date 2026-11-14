// Finding plugin folders, validating manifests, starting and stopping plugins through the plugin
// runtime, and saving their settings and the person's choice of plugins.
#include "mikserstudio/core/PluginManager.h"

#include "Logging.h"
#include "PluginSettingsStore.h"
#include "mikserstudio/core/JsonSchema.h"
#include "mikserstudio/core/PluginRuntime.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>

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
    std::unique_ptr<PluginInstance> instance;
    // Which start the runtime's callbacks belong to; 0 when the plugin isn't running.
    quint64 activation = 0;
};

struct SearchPath {
    QString directory;
    PluginSource source;
};

QString displayName(const Record& record)
{
    return record.state.manifest.name.isEmpty() ? record.state.manifest.id : record.state.manifest.name;
}

// The permissions a plugin runs with: what its manifest asks for, but only a built-in plugin
// manages plugins.
QStringList grantedFor(const PluginManifest& manifest, PluginSource source)
{
    QStringList granted = manifest.permissions;
    if (source != PluginSource::BuiltIn)
        granted.removeAll(u"plugins.manage"_s);
    return granted;
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

    // Off: the person disabled it, or it is one of their own that they haven't enabled.
    [[nodiscard]] bool isOff(const QString& id, PluginSource source) const
    {
        return choices.disabled.contains(id) || (source == PluginSource::User && !choices.enabled.contains(id));
    }

    Record readRecord(const QString& directory, PluginSource source) const;
    void loadStoredSettings(Record& record) const;
    void fail(const QString& id, const QString& message);
    void started(const QString& id, quint64 activation);
    void failedToStart(const QString& id, quint64 activation, const QString& message);

    PluginManager& q;
    CommandBus& bus;
    PluginSettingsStore store;
    PluginRuntime* runtime = nullptr;
    QList<SearchPath> searchPaths;
    PluginChoices choices;
    std::map<QString, Record> records;
    quint64 lastActivation = 0;
    bool activatedAll = false;
};

Record PluginManager::Impl::readRecord(const QString& directory, PluginSource source) const
{
    Record record;
    record.state.manifest.directory = QDir::fromNativeSeparators(QDir(directory).absolutePath());
    record.state.manifest.id = QDir(directory).dirName();
    record.state.status = PluginStatus::Failed;
    record.state.source = source;

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
    // A panel's QML runs JavaScript, with network access, on the interface's thread: only the plugins
    // Mikser Studio ships and those a developer points it to may bring one.
    if (source == PluginSource::User && !record.state.manifest.panels.isEmpty()) {
        qCWarning(lcPlugins).noquote() << "The panels of plugin" << record.state.manifest.id
                                       << "are not loaded: only built-in plugins and plugins in development may "
                                          "have QML panels";
        record.state.manifest.panels.clear();
    }
    record.state.status = isOff(record.state.manifest.id, source) ? PluginStatus::Disabled : PluginStatus::Discovered;
    record.state.granted = grantedFor(record.state.manifest, source);
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

void PluginManager::Impl::started(const QString& id, quint64 activation)
{
    Record* record = find(id);
    if (!record || record->activation != activation)
        return;
    record->state.status = PluginStatus::Active;
    record->state.error.clear();
    qCInfo(lcPlugins).noquote() << "Activated" << id;
    Q_EMIT q.pluginsChanged();
}

void PluginManager::Impl::failedToStart(const QString& id, quint64 activation, const QString& message)
{
    const Record* record = find(id);
    if (!record || record->activation != activation)
        return;
    // The instance stays: a plugin that reloads when its files change can start again.
    fail(id, message);
}

PluginManager::PluginManager(CommandBus& bus, const QString& settingsDirectory, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>(*this, bus, settingsDirectory))
{
    auto choices = d->store.loadChoices();
    if (choices)
        d->choices = std::move(*choices);
    else
        qCWarning(lcPlugins).noquote() << "The built-in plugins are on and the others off, because the plugin list "
                                          "could not be read:"
                                       << choices.error();
}

PluginManager::~PluginManager()
{
    for (auto& [id, record] : std::views::reverse(d->records)) {
        record.activation = 0;
        record.instance.reset();
    }
}

void PluginManager::setRuntime(PluginRuntime* runtime)
{
    d->runtime = runtime;
}

void PluginManager::addSearchPath(const QString& directory, PluginSource source)
{
    const QString path = QDir::fromNativeSeparators(QDir(directory).absolutePath());
    const bool known = std::ranges::any_of(d->searchPaths,
                                           [&path](const SearchPath& p) { return p.directory == path; });
    if (!known)
        d->searchPaths.append({path, source});
}

void PluginManager::discover()
{
    std::map<QString, Record> found;
    QList<std::pair<QString, QString>> failures;
    for (const SearchPath& path : std::as_const(d->searchPaths)) {
        const QDir root(path.directory);
        if (!root.exists()) {
            qCInfo(lcPlugins).noquote() << "Plugin folder" << QDir::toNativeSeparators(path.directory)
                                        << "does not exist";
            continue;
        }
        for (const QFileInfo& folder : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!QFile::exists(QDir(folder.absoluteFilePath()).filePath(QLatin1StringView(kPluginManifestFileName))))
                continue;
            Record record = d->readRecord(folder.absoluteFilePath(), path.source);
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
                    {id, u"Plugin “%1” in %2 is ignored because %3 already provides a plugin with that id."_s.arg(
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
        if (record.instance)
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
        return std::unexpected(u"There is no plugin “%1”. Check the plugin folders and rescan."_s.arg(id));
    if (record->instance && record->state.status != PluginStatus::Failed)
        return {};
    if (!record->manifestValid)
        return std::unexpected(record->state.error);
    if (record->state.status == PluginStatus::Disabled)
        return std::unexpected(u"“%1” is disabled. Enable it in the plugin manager first."_s.arg(displayName(*record)));
    if (!d->runtime) {
        const QString message = u"“%1” can't start: this Mikser Studio runs no plugins."_s.arg(displayName(*record));
        d->fail(id, message);
        return std::unexpected(message);
    }

    // Activating again a plugin that failed starts it anew.
    record->activation = 0;
    record->instance.reset();
    const quint64 activation = ++d->lastActivation;
    record->activation = activation;
    record->state.status = PluginStatus::Starting;
    record->state.error.clear();
    PluginActivation start{
        .granted = record->state.granted,
        .settings = settings(id),
        .started = [this, id, activation] { d->started(id, activation); },
        .failed = [this, id, activation](const QString& message) { d->failedToStart(id, activation, message); },
    };
    auto instance = d->runtime->activate(record->state.manifest, std::move(start));
    record = d->find(id);
    if (!instance) {
        if (record)
            record->activation = 0;
        d->fail(id, instance.error());
        return std::unexpected(instance.error());
    }
    if (!record)
        return std::unexpected(u"Plugin “%1” was removed while it started."_s.arg(id));
    record->instance = std::move(*instance);
    Q_EMIT pluginsChanged();
    return {};
}

void PluginManager::deactivate(const QString& id)
{
    Record* record = d->find(id);
    if (!record || !record->instance)
        return;
    std::unique_ptr<PluginInstance> instance = std::move(record->instance);
    record->activation = 0;
    record->state.status = d->isOff(id, record->state.source) ? PluginStatus::Disabled : PluginStatus::Discovered;
    record->state.error.clear();
    instance.reset();
    qCInfo(lcPlugins).noquote() << "Deactivated" << id;
    Q_EMIT pluginsChanged();
}

void PluginManager::activateAll()
{
    d->activatedAll = true;
    QStringList ids;
    for (const auto& [id, record] : d->records) {
        if (record.manifestValid && record.state.status == PluginStatus::Discovered)
            ids.append(id);
    }
    for (const QString& id : std::as_const(ids))
        std::ignore = activate(id);  // Failures are reported through pluginFailed().
}

bool PluginManager::ready() const
{
    return d->activatedAll && std::ranges::none_of(d->records, [](const auto& entry) {
               return entry.second.state.status == PluginStatus::Starting;
           });
}

std::expected<void, QString> PluginManager::setEnabled(const QString& id, bool enabled)
{
    Record* record = d->find(id);
    if (!record || !record->manifestValid)
        return std::unexpected(u"There is no usable plugin “%1”."_s.arg(id));

    // The person's own plugins are off until enabled; the others are on until disabled.
    PluginChoices choices = d->choices;
    if (record->state.source == PluginSource::User) {
        choices.disabled.remove(id);
        if (enabled)
            choices.enabled.insert(id);
        else
            choices.enabled.remove(id);
    } else if (enabled) {
        choices.disabled.remove(id);
    } else {
        choices.disabled.insert(id);
    }
    if (auto saved = d->store.saveChoices(choices); !saved)
        return std::unexpected(saved.error());
    d->choices = std::move(choices);

    if (enabled) {
        if (record->state.status == PluginStatus::Disabled)
            record->state.status = PluginStatus::Discovered;
        Q_EMIT pluginsChanged();
        return {};
    }
    if (record->instance)
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
        return std::unexpected(u"There is no usable plugin “%1”."_s.arg(id));
    const QJsonObject& schema = record->state.manifest.settingsSchema;
    if (schema.isEmpty() && !settings.isEmpty())
        return std::unexpected(u"“%1” has no settings."_s.arg(displayName(*record)));

    const QJsonObject effective = JsonSchema::applyDefaults(settings, schema);
    if (const QStringList problems = JsonSchema::validate(effective, schema); !problems.isEmpty())
        return std::unexpected(
            u"The settings for “%1” are not valid: %2"_s.arg(displayName(*record), problems.join(u"; "_s)));
    if (auto saved = d->store.save(id, settings); !saved)
        return std::unexpected(saved.error());

    record->storedSettings = settings;
    if (record->instance)
        record->instance->setSettings(effective);
    Q_EMIT settingsChanged(id, effective);
    return {};
}

}  // namespace mikserstudio::core
