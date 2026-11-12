#include "EngineTestSupport.h"

#include "MltProfile.h"
#include "MltResource.h"
#include "mikserstudio/engine/MltMediaProbe.h"

#include <Mlt.h>

#include <catch2/generators/catch_generators.hpp>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mikserstudio::core::Rational;
using mikserstudio::engine::isColorResource;
using mikserstudio::engine::makeProfile;
using mikserstudio::engine::MltMediaProbe;
using mikserstudio::engine::openProducer;

TEST_CASE("Only color resources are taken as MLT service strings", "[resource]")
{
    CHECK(isColorResource(u"color:#ff0000"_s));
    CHECK(isColorResource(u"color:red"_s));
    CHECK_FALSE(isColorResource(u"colour:#ff0000"_s));  // MLT's British alias of "color:"
    CHECK_FALSE(isColorResource(u"color:#ff0000 and more"_s));
    CHECK_FALSE(isColorResource(u"xml-string:<mlt/>"_s));
    CHECK_FALSE(isColorResource(u"D:/clips/color:red.mp4"_s));
}

TEST_CASE("Media paths that are not local files never reach MLT", "[resource]")
{
    mikserstudio::tests::mltRuntime();
    auto profile = makeProfile(Rational{25, 1}, 320, 180);

    // The XML entity hides "://" from a naive URL check; MLT would fetch the URL through FFmpeg.
    const QString refused = GENERATE(
        u"xml-string:<mlt><producer id=\"p\"><property name=\"mlt_service\">avformat</property>"
        u"<property name=\"resource\">http&#58;//127.0.0.1:9/t.mp4</property></producer></mlt>"_s,
        u"avformat:http&#58;//127.0.0.1:9/t.mp4"_s, u"http://127.0.0.1:9/t.mp4"_s, u"dshow:video=Camera"_s,
        u"gdigrab:desktop"_s, u"//server/share/clip.mp4"_s, u"\\\\server\\share\\clip.mp4"_s, u"clip.mp4"_s,
        u"color:#ff0000:extra"_s);
    CAPTURE(refused);
    const auto opened = openProducer(*profile, refused);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().contains(u"not a file on this computer"_s));

    MltMediaProbe probe(mikserstudio::tests::mltRuntime());
    CHECK_FALSE(probe.probe(refused, Rational{25, 1}).has_value());
}

TEST_CASE("Local MLT documents are refused, local media and colors open", "[resource]")
{
    mikserstudio::tests::mltRuntime();
    auto profile = makeProfile(Rational{25, 1}, 320, 180);
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    // An .mlt file names its own resources (URLs and devices included), so it is not media.
    const QString document = dir.filePath(u"remote.MLT"_s);
    QFile file(document);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(
        R"(<mlt><producer id="p"><property name="resource">http://127.0.0.1:9/t.mp4</property></producer></mlt>)");
    file.close();
    const auto refused = openProducer(*profile, document);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().contains(u"not a media file"_s));
    CHECK(refused.error().contains(QDir::toNativeSeparators(document)));

    QImage picture(32, 18, QImage::Format_RGB32);
    picture.fill(Qt::blue);
    const QString png = dir.filePath(u"still.png"_s);
    REQUIRE(picture.save(png));
    CHECK(openProducer(*profile, png).has_value());
    CHECK(openProducer(*profile, QDir::toNativeSeparators(png)).has_value());
    CHECK(openProducer(*profile, u"color:#00ff00"_s).has_value());
}
