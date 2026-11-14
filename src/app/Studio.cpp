// Creating the core, engine, AI engine, plugins and local API, registering their commands, and starting and
// stopping them in order.
#include "Studio.h"

#include "AppCommands.h"
#include "AppPaths.h"
#include "Logging.h"
#include "UnavailableProbe.h"

#include "mikserstudio/core/PluginCommands.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/ProjectSnapshot.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/engine/MltRuntime.h"
#include "mikserstudio/services/AiCommands.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

services::AiEngineOptions aiOptions(const AppPaths& paths)
{
    return services::AiEngineOptions{
        .cacheDirectory = QDir(paths.user.cache).filePath(u"ai"_s),
        .settingsFile = QDir(paths.user.config).filePath(u"ai-settings.json"_s),
    };
}

}  // namespace

Studio::Studio(const AppPaths& paths, StudioOptions options)
    : m_options(options)
    , m_ai(aiOptions(paths))
    , m_lua({.bus = &m_bus,
             .jobs = &m_jobs,
             .snapshot = [this] { return m_project.snapshot(); },
             .hotReload = options.pluginHotReload})
    , m_discoveryFile(paths.user.discoveryFile)
    , m_plugins(m_bus, QDir(paths.user.config).filePath(u"plugin-settings"_s))
{
    const auto audio = options.audio ? engine::AudioOutput::Device : engine::AudioOutput::Silent;
    if (const auto runtime = engine::MltRuntime::initialize(); runtime) {
        qCInfo(lcApp) << "MLT modules:" << (*runtime)->repositoryPath() << "data:" << (*runtime)->dataPath();
        m_probe = std::make_unique<engine::MltMediaProbe>(**runtime);
        m_source = std::make_unique<engine::ClipPlayer>(**runtime, audio);
        m_program = std::make_unique<engine::TimelinePlayer>(**runtime, m_project, audio);
    } else {
        m_engineError = runtime.error();
        qCCritical(lcApp) << "Playback is unavailable:" << m_engineError;
        m_probe = std::make_unique<UnavailableProbe>(m_engineError);
    }

    core::registerProjectCommands(m_bus, m_project, *m_probe);
    if (m_program)
        core::registerTransportCommands(m_bus, *m_program, u"program"_s, u"Program monitor"_s);
    if (m_source)
        core::registerTransportCommands(m_bus, *m_source, u"source"_s, u"Source monitor"_s);
    registerAppCommands(m_bus, m_project, m_source.get());
    if (m_source) {
        // The source monitor shows a media item of the project; a new or opened project replaces it.
        QObject::connect(&m_project, &core::ProjectModel::projectReset, m_source.get(), &engine::ClipPlayer::close);
    }

    services::registerAiCommands(m_bus, m_project, m_ai);
    core::registerJobCommands(m_bus, m_jobs);
    services::mirrorAiJobs(m_ai, m_jobs);

    m_plugins.setRuntime(&m_lua);
    m_plugins.addSearchPath(paths.pluginsDir, core::PluginSource::BuiltIn);
    if (options.safeMode) {
        qCInfo(lcApp) << "Safe mode: only the built-in plugins run";
    } else {
        m_plugins.addSearchPath(paths.userPluginsDir, core::PluginSource::User);
        for (const QString& folder : paths.developmentPluginsDirs)
            m_plugins.addSearchPath(folder, core::PluginSource::Development);
    }
    m_plugins.discover();
    core::registerPluginCommands(m_bus, m_plugins);
    QObject::connect(&m_plugins, &core::PluginManager::pluginFailed, [](const QString& id, const QString& message) {
        qCWarning(lcApp) << "Plugin" << id << "failed:" << message;
    });
}

Studio::~Studio()
{
    // Silence playback first: stopping plugins below can take seconds.
    if (m_program)
        m_program->pause();
    if (m_source)
        m_source->pause();
}

void Studio::start()
{
    if (m_options.api) {
        api::ApiServerOptions apiOptions;
        apiOptions.port = m_options.apiPort;
        apiOptions.discoveryFile = m_discoveryFile;
        apiOptions.pluginsReady = [this] { return m_plugins.ready(); };
        m_api = std::make_unique<api::ApiServer>(m_bus, apiOptions);
        if (const auto port = m_api->start(); port)
            qCInfo(lcApp) << "API listening on 127.0.0.1:" << *port << "discovery file:" << m_api->discoveryFilePath();
        else
            qCWarning(lcApp) << "The local API is off:" << port.error();
    }
    m_plugins.activateAll();
}

}  // namespace mikserstudio::app
