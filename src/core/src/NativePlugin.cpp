// Loading a native plugin library, checking its exported table and calling its activate and deactivate.
#include "NativePlugin.h"

#include "Logging.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

std::expected<const MikserStudioPluginV1*, QString> pluginTable(QLibrary& library)
{
    const auto entry = reinterpret_cast<MikserStudioPluginEntryFn>(library.resolve(MIKSERSTUDIO_PLUGIN_ENTRY_SYMBOL));
    if (!entry)
        return std::unexpected(u"the library does not export %1(); it is not a Mikser Studio plugin"_s.arg(
            QLatin1StringView(MIKSERSTUDIO_PLUGIN_ENTRY_SYMBOL)));
    const MikserStudioPluginV1* table = entry();
    if (!table)
        return std::unexpected(
            u"%1() returned no plugin table"_s.arg(QLatin1StringView(MIKSERSTUDIO_PLUGIN_ENTRY_SYMBOL)));
    if (table->struct_size < sizeof(MikserStudioPluginV1))
        return std::unexpected(
            u"the plugin table is too small (%1 bytes); the plugin is damaged"_s.arg(table->struct_size));
    if (table->api_version != MIKSERSTUDIO_PLUGIN_API_VERSION) {
        return std::unexpected(
            u"the library was built for plugin API version %1, this version of Mikser Studio provides version %2"_s
                .arg(table->api_version)
                .arg(MIKSERSTUDIO_PLUGIN_API_VERSION));
    }
    if (!table->activate || !table->deactivate)
        return std::unexpected(u"the plugin table has no activate or deactivate function"_s);
    return table;
}

}  // namespace

std::expected<std::unique_ptr<NativePlugin>, QString> NativePlugin::activate(const PluginManifest& manifest,
                                                                             CommandBus& bus,
                                                                             PluginHost::SettingsProvider settings)
{
    auto library = std::make_unique<QLibrary>(QDir(manifest.directory).filePath(manifest.entry));
    if (!library->load())
        return std::unexpected(u"Could not load the plugin library: %1"_s.arg(library->errorString()));

    auto table = pluginTable(*library);
    if (!table) {
        library->unload();
        return std::unexpected(
            u"Could not load %1: %2"_s.arg(QDir::toNativeSeparators(library->fileName()), table.error()));
    }

    auto host = std::make_unique<PluginHost>(bus, manifest.id, manifest.name, std::move(settings));
    void* state = nullptr;
    SinkBuffer error;
    if ((*table)->activate(host->api(), &state, error.sink()) != 0) {
        host->unregisterCommands();
        host.reset();
        library->unload();
        return std::unexpected(error.data().isEmpty() ? u"The plugin could not start and gave no reason."_s
                                                      : u"The plugin could not start: %1"_s.arg(error.text()));
    }

    std::unique_ptr<NativePlugin> plugin(new NativePlugin());
    plugin->m_library = std::move(library);
    plugin->m_table = *table;
    plugin->m_state = state;
    plugin->m_host = std::move(host);
    qCInfo(lcPlugins).noquote() << "Activated" << manifest.id << "with commands" << plugin->commands().join(u", "_s);
    return plugin;
}

NativePlugin::~NativePlugin()
{
    m_host->unregisterCommands();
    m_table->deactivate(m_state);
    m_host.reset();
    if (!m_library->unload())
        qCWarning(lcPlugins).noquote() << "Could not unload" << m_library->fileName() << "-"
                                       << m_library->errorString();
}

}  // namespace mikserstudio::core
