// The MLT factory: initializing MLT, finding its modules and data, and picking the video compositor.
#include "mikserstudio/engine/MltRuntime.h"

#include "Logging.h"
#include "MltLog.h"
#include "MltPaths.h"

#include <Mlt.h>

#include <QDir>

#include <memory>
#include <mutex>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

QStringList serviceNames(Mlt::Properties* list)
{
    std::unique_ptr<Mlt::Properties> owned(list);
    QStringList names;
    if (!owned)
        return names;
    for (int i = 0; i < owned->count(); ++i) {
        if (const char* name = owned->get_name(i))
            names.append(QString::fromUtf8(name));
    }
    return names;
}

QString describe(const MltLocation& location)
{
    return location.path.isEmpty() ? location.source : u"%1 (%2)"_s.arg(location.path, location.source);
}

}  // namespace

std::expected<const MltRuntime*, QString> MltRuntime::initialize()
{
    static std::once_flag once;
    static std::expected<const MltRuntime*, QString> result = std::unexpected(QString());
    std::call_once(once, [] { result = create(); });
    return result;
}

std::expected<const MltRuntime*, QString> MltRuntime::create()
{
    auto repository = findMltRepository();
    if (!repository)
        return std::unexpected(repository.error());
    auto data = findMltData();
    if (!data)
        return std::unexpected(data.error());

    // MLT reads MLT_DATA once, during factory initialization.
    if (!data->path.isEmpty())
        qputenv("MLT_DATA", QDir::toNativeSeparators(data->path).toUtf8());

    installMltLogHandler();
    const QByteArray directory = QDir::toNativeSeparators(repository->path).toUtf8();
    // The returned wrapper refers to the factory's global repository and stays alive with it.
    Mlt::Repository* mltRepository = Mlt::Factory::init(directory.isEmpty() ? nullptr : directory.constData());
    if (!mltRepository)
        return std::unexpected(u"The MLT media framework could not be started. Reinstall Mikser Studio."_s);

    std::unique_ptr<MltRuntime> runtime(new MltRuntime);
    runtime->m_repositoryPath = QDir::fromNativeSeparators(QString::fromUtf8(mlt_factory_directory()));
    runtime->m_repositorySource = repository->source;
    runtime->m_dataPath = data->path;
    runtime->m_producers = serviceNames(mltRepository->producers());
    runtime->m_consumers = serviceNames(mltRepository->consumers());
    runtime->m_transitions = serviceNames(mltRepository->transitions());
    for (const QString& compositor : {u"qtblend"_s, u"frei0r.cairoblend"_s, u"composite"_s}) {
        if (runtime->hasTransition(compositor)) {
            runtime->m_videoCompositor = compositor;
            break;
        }
    }

    QStringList missing;
    for (const QString& producer : {u"avformat"_s, u"color"_s}) {
        if (!runtime->hasProducer(producer))
            missing.append(u"producer “%1”"_s.arg(producer));
    }
    if (!runtime->hasConsumer(u"null"_s))
        missing.append(u"consumer “null”"_s);
    if (!runtime->hasTransition(u"mix"_s))
        missing.append(u"transition “mix”"_s);
    if (runtime->m_videoCompositor.isEmpty())
        missing.append(u"a video compositing transition (qtblend, frei0r.cairoblend or composite)"_s);
    if (!missing.isEmpty()) {
        const QString message =
            u"Mikser Studio cannot play media because MLT modules are missing (%1). MLT modules were loaded from %2, "
            u"data from %3. Reinstall Mikser Studio, or set MIKSERSTUDIO_MLT_REPOSITORY and MIKSERSTUDIO_MLT_DATA to a complete "
            u"MLT 7 installation."_s.arg(missing.join(u", "_s), describe(*repository), describe(*data));
        return std::unexpected(message);
    }

    qCInfo(lcEngine).noquote() << "MLT" << QString::fromUtf8(mlt_version_get_string())
                               << "modules:" << describe(*repository) << "data:" << describe(*data)
                               << "video compositor:" << runtime->m_videoCompositor;
    return runtime.release();  // Lives for the whole process, like the MLT factory.
}

}  // namespace mikserstudio::engine
