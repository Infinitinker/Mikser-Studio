#include "EngineTestSupport.h"

#include "MltPaths.h"
#include "mikserstudio/engine/MltRuntime.h"

#include <QDir>

using namespace Qt::StringLiterals;
using mikserstudio::engine::MltRuntime;

TEST_CASE("MLT initializes once with the services playback needs", "[mlt]")
{
    const auto first = MltRuntime::initialize();
    REQUIRE(first.has_value());
    const auto second = MltRuntime::initialize();
    REQUIRE(second.has_value());
    CHECK(*first == *second);

    const MltRuntime& runtime = **first;
    CHECK_FALSE(runtime.repositoryPath().isEmpty());
    CHECK_FALSE(QDir(runtime.repositoryPath()).entryList({u"*mltcore*"_s}, QDir::Files).isEmpty());
    CHECK_FALSE(runtime.repositorySource().isEmpty());
    CHECK(runtime.hasProducer(u"avformat"_s));
    CHECK(runtime.hasProducer(u"color"_s));
    CHECK(runtime.hasConsumer(u"null"_s));
    CHECK(runtime.hasTransition(u"mix"_s));
    CHECK(runtime.hasTransition(runtime.videoCompositor()));
    CHECK_FALSE(runtime.hasProducer(u"no-such-producer"_s));
}

TEST_CASE("An MLT override pointing to a wrong folder is reported in user terms", "[mlt]")
{
    const QByteArray previous = qgetenv("MIKSERSTUDIO_MLT_REPOSITORY");
    qputenv("MIKSERSTUDIO_MLT_REPOSITORY", QDir::tempPath().toUtf8());
    const auto repository = mikserstudio::engine::findMltRepository();
    if (previous.isEmpty())
        qunsetenv("MIKSERSTUDIO_MLT_REPOSITORY");
    else
        qputenv("MIKSERSTUDIO_MLT_REPOSITORY", previous);

    REQUIRE_FALSE(repository.has_value());
    CHECK(repository.error().contains(u"MIKSERSTUDIO_MLT_REPOSITORY"_s));
    CHECK(repository.error().contains(u"does not contain the MLT modules"_s));
}

TEST_CASE("MLT folders are found without any environment override", "[mlt]")
{
    if (qEnvironmentVariableIsSet("MIKSERSTUDIO_MLT_REPOSITORY") || qEnvironmentVariableIsSet("MIKSERSTUDIO_MLT_DATA"))
        SKIP("An MIKSERSTUDIO_MLT_* override is set");
    const auto repository = mikserstudio::engine::findMltRepository();
    REQUIRE(repository.has_value());
    CHECK_FALSE(repository->path.isEmpty());
    const auto data = mikserstudio::engine::findMltData();
    REQUIRE(data.has_value());
    CHECK(QDir(data->path).exists(u"profiles"_s));
}
