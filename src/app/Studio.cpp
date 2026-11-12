// Creating the core, engine, AI service, plugins and local API, registering their commands, and starting and
// stopping them in order.
#include "Studio.h"

#include "AppCommands.h"
#include "AppPaths.h"
#include "Logging.h"
#include "UnavailableProbe.h"

#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/engine/MltRuntime.h"
#include "mikserstudio/services/AiCommands.h"

#include <QDir>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

services::SidecarOptions sidecarOptions(const AppPaths& paths)
{
    services::SidecarOptions options;
    options.aiDirectory = paths.aiDir;
    options.cacheDirectory = QDir(paths.cacheDir).filePath(u"ai"_s);
    options.ffmpegPath = paths.ffmpegPath;
    return options;
}

}  // namespace

Studio::Studio(const AppPaths& paths, StudioOptions options)
    : m_options(options)
    , m_sidecar(sidecarOptions(paths))
    , m_plugins(m_bus, QDir(paths.configDir).filePath(u"plugins"_s))
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
        core::registerTransportCommands(m_bus, *m_program, u"program"_s, u"Program Monitor"_s);
    if (m_source)
        core::registerTransportCommands(m_bus, *m_source, u"source"_s, u"Source Monitor"_s);
    registerAppCommands(m_bus, m_project, m_source.get());
    if (m_source) {
        // The source monitor shows a media item of the project; a new or opened project replaces it.
        QObject::connect(&m_project, &core::ProjectModel::projectReset, m_source.get(), &engine::ClipPlayer::close);
    }

    if (paths.aiDir.isEmpty())
        qCInfo(lcApp) << "No AI service in this build (ai/ was not found): the AI commands say so.";
    services::registerAiCommands(m_bus, m_project, m_sidecar, m_aiClient);

    m_plugins.addSearchPath(paths.pluginsDir);
    m_plugins.discover();
    QObject::connect(&m_plugins, &core::PluginManager::pluginFailed, [](const QString& id, const QString& message) {
        qCWarning(lcApp) << "Plugin" << id << "failed:" << message;
    });
}

Studio::~Studio()
{
    // Silence playback first: stopping plugins and the AI service below can take seconds.
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
        m_api = std::make_unique<api::ApiServer>(m_bus, apiOptions);
        if (const auto port = m_api->start(); port)
            qCInfo(lcApp) << "API listening on 127.0.0.1:" << *port << "discovery file:" << m_api->discoveryFilePath();
        else
            qCWarning(lcApp) << "The local API is off:" << port.error();
    }
    m_plugins.activateAll();
}

}  // namespace mikserstudio::app
