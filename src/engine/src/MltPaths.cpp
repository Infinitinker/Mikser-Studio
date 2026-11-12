#include "MltPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

struct Candidate {
    QString path;
    QString source;
};

bool hasModules(const QString& path)
{
    const QDir dir(path);
    return dir.exists() && !dir.entryList({u"*mltcore*"_s}, QDir::Files).isEmpty();
}

bool hasData(const QString& path)
{
    return QFileInfo::exists(path + u"/core/loader.dict"_s) && QFileInfo(path + u"/profiles"_s).isDir();
}

QList<Candidate> executableCandidates(const QString& kind)
{
    QList<Candidate> candidates;
    if (!QCoreApplication::instance())
        return candidates;
    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString& relative : {u"/../%1/mlt"_s, u"/../%1/mlt-7"_s, u"/%1/mlt"_s, u"/%1/mlt-7"_s})
        candidates.append({QDir::cleanPath(exeDir + relative.arg(kind)), u"next to the executable"_s});
    return candidates;
}

std::expected<MltLocation, QString> find(const char* overrideVariable, const QList<Candidate>& extraEnvironment,
                                         const QString& kind, const QString& buildTimePath,
                                         bool (*isValid)(const QString&))
{
    const QString overridePath = qEnvironmentVariable(overrideVariable);
    if (!overridePath.isEmpty()) {
        if (!isValid(overridePath))
            return std::unexpected(u"%1 is set to \"%2\", but that folder does not contain the MLT %3. Point it to the "
                                   u"MLT %3 folder of your installation or unset it."_s.arg(
                                       QString::fromLatin1(overrideVariable), overridePath,
                                       kind == u"lib"_s ? u"modules"_s : u"data files"_s));
        return MltLocation{QDir::cleanPath(overridePath), QString::fromLatin1(overrideVariable)};
    }

    QList<Candidate> candidates = extraEnvironment;
    candidates.append(executableCandidates(kind));
    if (!buildTimePath.isEmpty())
        candidates.append({buildTimePath, u"build-time install"_s});
    for (const Candidate& candidate : candidates) {
        if (isValid(candidate.path))
            return MltLocation{candidate.path, candidate.source};
    }
    return MltLocation{{}, u"MLT default"_s};
}

}  // namespace

std::expected<MltLocation, QString> findMltRepository()
{
    return find("MIKSERSTUDIO_MLT_REPOSITORY", {}, u"lib"_s, QStringLiteral(MIKSERSTUDIO_MLT_BUILD_REPOSITORY),
                hasModules);
}

std::expected<MltLocation, QString> findMltData()
{
    QList<Candidate> environment;
    if (const QString mltData = qEnvironmentVariable("MLT_DATA"); !mltData.isEmpty())
        environment.append({mltData, u"MLT_DATA"_s});
    return find("MIKSERSTUDIO_MLT_DATA", environment, u"share"_s, QStringLiteral(MIKSERSTUDIO_MLT_BUILD_DATA), hasData);
}

}  // namespace mikserstudio::engine
