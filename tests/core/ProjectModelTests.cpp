#include "TestSupport.h"

#include "mikserstudio/core/Project.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QRegularExpression>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

// An absolute path on every platform ("C:/media/..." or "/media/...").
QString mediaPath(const QString& fileName)
{
    return QDir::rootPath() + u"media/"_s + fileName;
}

MediaItem videoItem(const QString& id, FramePos duration = 300)
{
    return MediaItem{
        .id = id,
        .path = mediaPath(id + u".mp4"_s),
        .name = {},
        .info = MediaInfo{.kind = MediaKind::Video,
                          .durationFrames = duration,
                          .width = 1920,
                          .height = 1080,
                          .frameRate = {30, 1},
                          .hasVideo = true,
                          .hasAudio = true,
                          .audioSampleRate = 48000,
                          .audioChannels = 2},
        .provenance = {},
    };
}

MediaItem audioItem(const QString& id, FramePos duration = 300)
{
    MediaItem item = videoItem(id, duration);
    item.path = mediaPath(id + u".wav"_s);
    item.info.kind = MediaKind::Audio;
    item.info.hasVideo = false;
    item.info.width = 0;
    item.info.height = 0;
    return item;
}

MediaItem imageItem(const QString& id)
{
    MediaItem item = videoItem(id, 150);
    item.path = mediaPath(id + u".png"_s);
    item.info.kind = MediaKind::Image;
    item.info.hasAudio = false;
    return item;
}

Clip clip(const QString& id, const QString& media, const QString& track, FramePos position, FramePos in, FramePos out)
{
    return Clip{.id = id, .mediaId = media, .trackId = track, .position = position, .in = in, .out = out};
}

}  // namespace

TEST_CASE("A new project has four tracks and default format", "[project]")
{
    ProjectModel project;
    CHECK(project.frameRate() == Rational{30, 1});
    CHECK(project.width() == 1920);
    CHECK(project.height() == 1080);
    REQUIRE(project.tracks().size() == 4);
    CHECK(project.tracks()[0].id == u"V1"_s);
    CHECK(project.tracks()[1].id == u"V2"_s);
    CHECK(project.tracks()[2].id == u"A1"_s);
    CHECK(project.tracks()[2].kind == TrackKind::Audio);
    CHECK(project.tracks()[3].id == u"A2"_s);
    CHECK(project.media().isEmpty());
    CHECK(project.duration() == 0);

    project.reset({25, 1}, 1280, 720);
    CHECK(project.frameRate() == Rational{25, 1});
    CHECK(project.width() == 1280);
}

TEST_CASE("Media can be added, found and removed", "[project]")
{
    ProjectModel project;
    QStringList added;
    QObject::connect(&project, &ProjectModel::mediaAdded, [&](const QString& id) { added.append(id); });

    REQUIRE(project.addMedia(videoItem(u"m1"_s)));
    REQUIRE(project.addMedia(videoItem(u"m2"_s)));
    REQUIRE(project.addMedia(videoItem(u"m0"_s), 0));
    CHECK(added == QStringList{u"m1"_s, u"m2"_s, u"m0"_s});
    CHECK(project.media()[0].id == u"m0"_s);
    CHECK(project.findMedia(u"m1"_s)->name == u"m1.mp4"_s);

    CHECK_FALSE(project.addMedia(videoItem(u"m1"_s)));
    CHECK_FALSE(project.addMedia(videoItem(QString())));
    CHECK_FALSE(project.addMedia(videoItem(u"m9"_s), 9));
    MediaItem noPath = videoItem(u"m8"_s);
    noPath.path.clear();
    CHECK_FALSE(project.addMedia(noPath));

    const auto removed = project.removeMedia(u"m1"_s);
    REQUIRE(removed);
    CHECK(removed->id == u"m1"_s);
    CHECK(project.findMedia(u"m1"_s) == nullptr);
    CHECK_FALSE(project.removeMedia(u"m1"_s));
}

TEST_CASE("Media used by clips cannot be removed", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(videoItem(u"m1"_s)));
    REQUIRE(project.addClip(clip(u"c1"_s, u"m1"_s, u"V1"_s, 0, 0, 100)));
    const auto removed = project.removeMedia(u"m1"_s);
    REQUIRE_FALSE(removed);
    CHECK(removed.error().contains(u"1 clip"_s));
    REQUIRE(project.removeClip(u"c1"_s));
    CHECK(project.removeMedia(u"m1"_s));
}

TEST_CASE("Clips are validated against media, tracks and each other", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(videoItem(u"video"_s, 300)));
    REQUIRE(project.addMedia(audioItem(u"audio"_s, 300)));
    REQUIRE(project.addMedia(imageItem(u"image"_s)));
    REQUIRE(project.addClip(clip(u"c1"_s, u"video"_s, u"V1"_s, 100, 0, 100)));  // Frames 100-200.

    SECTION("bad references")
    {
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"missing"_s, u"V1"_s, 0, 0, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V9"_s, 0, 0, 10)));
        CHECK_FALSE(project.addClip(clip(u"c1"_s, u"video"_s, u"V2"_s, 0, 0, 10)));
        CHECK_FALSE(project.addClip(clip(QString(), u"video"_s, u"V2"_s, 0, 0, 10)));
    }
    SECTION("bad ranges")
    {
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V2"_s, -1, 0, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V2"_s, 0, -1, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V2"_s, 0, 10, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V2"_s, 0, 0, 301)));
        CHECK(project.addClip(clip(u"x"_s, u"video"_s, u"V2"_s, 0, 0, 300)));
        CHECK(project.addClip(clip(u"still"_s, u"image"_s, u"V2"_s, 300, 0, 1000)));
    }
    SECTION("track kinds")
    {
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"audio"_s, u"V2"_s, 0, 0, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"image"_s, u"A1"_s, 0, 0, 10)));
        CHECK(project.addClip(clip(u"a"_s, u"audio"_s, u"A1"_s, 0, 0, 10)));
        CHECK(project.addClip(clip(u"va"_s, u"video"_s, u"A2"_s, 0, 0, 10)));
    }
    SECTION("overlaps on the same track")
    {
        const auto overlap = project.addClip(clip(u"x"_s, u"video"_s, u"V1"_s, 150, 0, 100));
        REQUIRE_FALSE(overlap);
        CHECK(overlap.error().contains(u"c1"_s));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V1"_s, 50, 0, 51)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V1"_s, 120, 0, 10)));
        CHECK_FALSE(project.addClip(clip(u"x"_s, u"video"_s, u"V1"_s, 0, 0, 300)));
        CHECK(project.addClip(clip(u"before"_s, u"video"_s, u"V1"_s, 0, 0, 100)));  // Ends where c1 starts.
        CHECK(project.addClip(clip(u"after"_s, u"video"_s, u"V1"_s, 200, 0, 50)));  // Starts where c1 ends.
        CHECK(project.addClip(clip(u"other"_s, u"video"_s, u"V2"_s, 150, 0, 100)));
    }
}

TEST_CASE("Clips stay sorted by track order and position", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(videoItem(u"m"_s, 1000)));
    REQUIRE(project.addClip(clip(u"a1"_s, u"m"_s, u"A1"_s, 0, 0, 10)));
    REQUIRE(project.addClip(clip(u"v2"_s, u"m"_s, u"V2"_s, 0, 0, 10)));
    REQUIRE(project.addClip(clip(u"v1-late"_s, u"m"_s, u"V1"_s, 500, 0, 10)));
    REQUIRE(project.addClip(clip(u"v1-early"_s, u"m"_s, u"V1"_s, 10, 0, 10)));

    const auto order = [&project] {
        QStringList ids;
        for (const Clip& c : project.clips())
            ids.append(c.id);
        return ids;
    };
    CHECK(order() == QStringList{u"v1-early"_s, u"v1-late"_s, u"v2"_s, u"a1"_s});

    REQUIRE(project.moveClip(u"v1-late"_s, u"V1"_s, 0));
    CHECK(order() == QStringList{u"v1-late"_s, u"v1-early"_s, u"v2"_s, u"a1"_s});
    CHECK(project.duration() == 20);
}

TEST_CASE("Clips can be moved and trimmed within the rules", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(videoItem(u"m"_s, 300)));
    REQUIRE(project.addClip(clip(u"c1"_s, u"m"_s, u"V1"_s, 0, 0, 100)));
    REQUIRE(project.addClip(clip(u"c2"_s, u"m"_s, u"V1"_s, 200, 0, 100)));
    QStringList changed;
    int timelineChanges = 0;
    QObject::connect(&project, &ProjectModel::clipChanged, [&](const QString& id) { changed.append(id); });
    QObject::connect(&project, &ProjectModel::timelineChanged, [&] { ++timelineChanges; });

    CHECK_FALSE(project.moveClip(u"c1"_s, u"V1"_s, 150));
    CHECK_FALSE(project.moveClip(u"c1"_s, u"V1"_s, -5));
    CHECK_FALSE(project.moveClip(u"c1"_s, u"V7"_s, 0));
    CHECK_FALSE(project.moveClip(u"nope"_s, u"V1"_s, 0));
    REQUIRE(project.moveClip(u"c1"_s, u"V1"_s, 100));  // Touches c2 but does not overlap.
    CHECK(project.findClip(u"c1"_s)->position == 100);
    REQUIRE(project.moveClip(u"c1"_s, u"V1"_s, 50));  // Overlaps only its own old place.
    REQUIRE(project.moveClip(u"c1"_s, u"V2"_s, 250));
    CHECK(project.findClip(u"c1"_s)->trackId == u"V2"_s);

    CHECK_FALSE(project.trimClip(u"c2"_s, 0, 301));
    CHECK_FALSE(project.trimClip(u"c2"_s, 50, 50));
    REQUIRE(project.trimClip(u"c2"_s, 10, 60));
    const Clip* trimmed = project.findClip(u"c2"_s);
    CHECK(trimmed->position == 200);
    CHECK(trimmed->in == 10);
    CHECK(trimmed->out == 60);
    CHECK(trimmed->length() == 50);
    CHECK(trimmed->end() == 250);

    REQUIRE(project.moveClip(u"c1"_s, u"V1"_s, 0));
    CHECK_FALSE(project.trimClip(u"c1"_s, 0, 250));  // Would run into c2 at frame 200.

    CHECK(changed == QStringList{u"c1"_s, u"c1"_s, u"c1"_s, u"c2"_s, u"c1"_s});
    CHECK(timelineChanges == 5);
}

TEST_CASE("Projects round-trip through JSON", "[project]")
{
    ProjectModel project;
    project.reset({24000, 1001}, 3840, 2160);
    MediaItem generated = imageItem(u"gen"_s);
    generated.name = u"Generated"_s;
    generated.provenance = json(R"({"workflow": "upscale", "seed": 42})");
    REQUIRE(project.addMedia(videoItem(u"m1"_s)));
    REQUIRE(project.addMedia(audioItem(u"m2"_s)));
    REQUIRE(project.addMedia(generated));
    REQUIRE(project.addClip(clip(u"c1"_s, u"m1"_s, u"V1"_s, 10, 5, 95)));
    REQUIRE(project.addClip(clip(u"c2"_s, u"m2"_s, u"A1"_s, 0, 0, 300)));
    REQUIRE(project.addClip(clip(u"c3"_s, u"gen"_s, u"V2"_s, 0, 0, 150)));

    const QJsonObject saved = project.toJson();
    CHECK(saved.value(u"version").toInt() == ProjectModel::kFormatVersion);

    ProjectModel loaded;
    int resets = 0;
    QObject::connect(&loaded, &ProjectModel::projectReset, [&] { ++resets; });
    REQUIRE(loaded.loadJson(saved));
    CHECK(resets == 1);
    CHECK(loaded.toJson() == saved);
    CHECK(loaded.frameRate() == Rational{24000, 1001});
    CHECK(loaded.width() == 3840);
    REQUIRE(loaded.media().size() == 3);
    CHECK(loaded.media()[2].provenance.value(u"seed").toInt() == 42);
    CHECK(loaded.media()[2].info.kind == MediaKind::Image);
    CHECK(loaded.media()[1].info.audioSampleRate == 48000);
    CHECK(loaded.findClip(u"c1"_s)->in == 5);
}

TEST_CASE("Loading a broken project leaves the current one untouched", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(videoItem(u"keep"_s)));
    const QJsonObject before = project.toJson();

    ProjectModel source;
    REQUIRE(source.addMedia(videoItem(u"m1"_s)));
    REQUIRE(source.addClip(clip(u"c1"_s, u"m1"_s, u"V1"_s, 0, 0, 100)));
    const QJsonObject good = source.toJson();

    SECTION("missing version")
    {
        QJsonObject broken = good;
        broken.remove(u"version"_s);
        CHECK_FALSE(project.loadJson(broken));
    }
    SECTION("newer version")
    {
        QJsonObject broken = good;
        broken.insert(u"version"_s, ProjectModel::kFormatVersion + 1);
        const auto result = project.loadJson(broken);
        REQUIRE_FALSE(result);
        CHECK(result.error().contains(u"newer Mikser Studio"_s));
    }
    SECTION("schema violation")
    {
        QJsonObject broken = good;
        broken.insert(u"width"_s, u"wide"_s);
        const auto result = project.loadJson(broken);
        REQUIRE_FALSE(result);
        CHECK(result.error().contains(u"/width"_s));
    }
    SECTION("overlapping clips")
    {
        QJsonObject broken = good;
        QJsonArray clips = broken.value(u"clips").toArray();
        QJsonObject second = clips[0].toObject();
        second.insert(u"id"_s, u"c2"_s);
        second.insert(u"position"_s, 50);
        clips.append(second);
        broken.insert(u"clips"_s, clips);
        CHECK_FALSE(project.loadJson(broken));
    }
    SECTION("clip referencing unknown media")
    {
        QJsonObject broken = good;
        broken.insert(u"media"_s, QJsonArray());
        CHECK_FALSE(project.loadJson(broken));
    }
    SECTION("duplicate track ids")
    {
        QJsonObject broken = good;
        QJsonArray tracks = broken.value(u"tracks").toArray();
        tracks.append(tracks[0]);
        broken.insert(u"tracks"_s, tracks);
        CHECK_FALSE(project.loadJson(broken));
    }
    CHECK(project.toJson() == before);
}

TEST_CASE("newId creates unique prefixed ids", "[project]")
{
    const QString first = ProjectModel::newId(u"clip"_s);
    const QString second = ProjectModel::newId(u"clip"_s);
    CHECK(first != second);
    static const QRegularExpression format(u"^clip-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"_s);
    CHECK(format.match(first).hasMatch());
    CHECK(toString(MediaKind::Image) == u"image"_s);
    CHECK(toString(TrackKind::Audio) == u"audio"_s);
}

TEST_CASE("Frame values are capped so clip ends cannot overflow", "[project]")
{
    ProjectModel project;
    REQUIRE(project.addMedia(imageItem(u"image"_s)));
    REQUIRE(project.addMedia(videoItem(u"video"_s)));
    REQUIRE(project.addClip(clip(u"a"_s, u"image"_s, u"V1"_s, 0, 0, 100)));

    // position + length would wrap around to a negative end and slip past the overlap check.
    const auto wrapped = project.addClip(clip(u"b"_s, u"image"_s, u"V1"_s, 50, 0, 9223372036854775800));
    REQUIRE_FALSE(wrapped);
    CHECK(wrapped.error().contains(u"last frame Mikser Studio supports"_s));
    CHECK_FALSE(project.addClip(clip(u"b"_s, u"image"_s, u"V2"_s, kMaxFrame, 0, 1)));
    CHECK_FALSE(project.addClip(clip(u"b"_s, u"image"_s, u"V2"_s, 1, 0, kMaxFrame)));
    CHECK_FALSE(project.addClip(clip(u"b"_s, u"image"_s, u"V2"_s, 0, kMaxFrame, kMaxFrame + 1)));
    REQUIRE(project.addClip(clip(u"b"_s, u"image"_s, u"V2"_s, kMaxFrame - 10, 0, 10)));  // Ends exactly at the cap.
    CHECK(project.findClip(u"b"_s)->end() == kMaxFrame);
    CHECK_FALSE(project.moveClip(u"b"_s, u"V2"_s, kMaxFrame - 9));
    CHECK_FALSE(project.trimClip(u"b"_s, 0, 11));
    CHECK(project.duration() == kMaxFrame);

    const auto tooLong = project.addMedia(videoItem(u"endless"_s, kMaxFrame + 1));
    REQUIRE_FALSE(tooLong);
    CHECK(tooLong.error().contains(u"at most"_s));
}

TEST_CASE("Loading rejects frame and size values outside the supported range", "[project]")
{
    ProjectModel source;
    REQUIRE(source.addMedia(imageItem(u"m1"_s)));
    REQUIRE(source.addClip(clip(u"a"_s, u"m1"_s, u"V1"_s, 0, 0, 100)));
    const QJsonObject good = source.toJson();

    ProjectModel project;
    const QJsonObject before = project.toJson();
    const auto withClipValue = [&good](const QString& key, double value) {
        QJsonObject broken = good;
        QJsonArray clips = broken.value(u"clips"_s).toArray();
        QJsonObject second = clips[0].toObject();
        second.insert(u"id"_s, u"b"_s);
        second.insert(u"position"_s, 50);
        second.insert(key, value);
        clips.append(second);
        broken.insert(u"clips"_s, clips);
        return broken;
    };

    const auto overflow = project.loadJson(withClipValue(u"out"_s, 9223372036854775800.0));
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error().contains(u"/clips/1/out"_s));
    CHECK_FALSE(project.loadJson(withClipValue(u"position"_s, 1e19)));

    QJsonObject wide = good;
    wide.insert(u"width"_s, 3000000000.0);
    const auto tooWide = project.loadJson(wide);
    REQUIRE_FALSE(tooWide);
    CHECK(tooWide.error().contains(u"/width"_s));

    QJsonObject badRate = good;
    badRate.insert(u"frameRate"_s, QJsonObject{{u"num"_s, 30}, {u"den"_s, 5000000000.0}});
    const auto zeroRate = project.loadJson(badRate);
    REQUIRE_FALSE(zeroRate);
    CHECK(zeroRate.error().contains(u"/frameRate/den"_s));

    QJsonObject badInfo = good;
    QJsonArray media = badInfo.value(u"media"_s).toArray();
    QJsonObject item = media[0].toObject();
    QJsonObject info = item.value(u"info"_s).toObject();
    info.insert(u"audioSampleRate"_s, 3000000000.0);
    item.insert(u"info"_s, info);
    media[0] = item;
    badInfo.insert(u"media"_s, media);
    CHECK_FALSE(project.loadJson(badInfo));

    CHECK(project.toJson() == before);
}

TEST_CASE("Loading accepts only local media paths and color resources", "[project]")
{
    ProjectModel source;
    REQUIRE(source.addMedia(imageItem(u"m1"_s)));
    const QJsonObject good = source.toJson();
    const auto withPath = [&good](const QString& path) {
        QJsonObject document = good;
        QJsonArray media = document.value(u"media"_s).toArray();
        QJsonObject item = media[0].toObject();
        item.insert(u"path"_s, path);
        media[0] = item;
        document.insert(u"media"_s, media);
        return document;
    };

    ProjectModel project;
    for (
        const QString& path : {
            uR"(xml-string:<mlt><producer id="p"><property name="resource">http&#58;//example.invalid/t.mp4</property></producer></mlt>)"_s,
            u"http://example.invalid/clip.mp4"_s,
            u"file:///media/clip.mp4"_s,
            u"dshow:video=Camera"_s,
            u"gdigrab:desktop"_s,
            u"//server/share/clip.mp4"_s,
            uR"(\\server\share\clip.mp4)"_s,
            u"media/clip.mp4"_s,
            u"color:#ff0000; rm"_s,
        }) {
        INFO(path.toStdString());
        const auto loaded = project.loadJson(withPath(path));
        REQUIRE_FALSE(loaded);
        CHECK(loaded.error().contains(u"not an absolute path to a file on this computer"_s));
    }
    CHECK(project.media().isEmpty());

    for (const QString& path : {mediaPath(u"clip.mp4"_s), u"color:#ff0000"_s, u"color:red"_s, u"color:#80ff0000"_s}) {
        INFO(path.toStdString());
        REQUIRE(project.loadJson(withPath(path)));
        CHECK(project.media()[0].path == path);
    }
}

TEST_CASE("isAllowedMediaPath accepts local files and colors only", "[project]")
{
    CHECK(isAllowedMediaPath(mediaPath(u"a.mp4"_s)));
    CHECK(isAllowedMediaPath(QDir::toNativeSeparators(mediaPath(u"a.mp4"_s))));
    CHECK(isAllowedMediaPath(u"color:#00ff00"_s));
    CHECK_FALSE(isAllowedMediaPath(QString()));
    CHECK_FALSE(isAllowedMediaPath(u"a.mp4"_s));
    CHECK_FALSE(isAllowedMediaPath(u"avformat:/media/a.mp4"_s));
    CHECK_FALSE(isAllowedMediaPath(u"https://example.invalid/a.mp4"_s));
    CHECK_FALSE(isAllowedMediaPath(u"//server/share/a.mp4"_s));
}

TEST_CASE("Loading checks clip ids and still finds overlaps between neighbours", "[project]")
{
    ProjectModel source;
    REQUIRE(source.addMedia(videoItem(u"m1"_s, 1000)));
    REQUIRE(source.addClip(clip(u"a"_s, u"m1"_s, u"V1"_s, 0, 0, 100)));
    REQUIRE(source.addClip(clip(u"b"_s, u"m1"_s, u"V1"_s, 100, 0, 100)));
    REQUIRE(source.addClip(clip(u"c"_s, u"m1"_s, u"V1"_s, 200, 0, 100)));
    const QJsonObject good = source.toJson();

    ProjectModel project;
    QJsonObject duplicate = good;
    QJsonArray clips = duplicate.value(u"clips"_s).toArray();
    QJsonObject copy = clips[0].toObject();
    copy.insert(u"trackId"_s, u"V2"_s);
    clips.append(copy);
    duplicate.insert(u"clips"_s, clips);
    const auto twice = project.loadJson(duplicate);
    REQUIRE_FALSE(twice);
    CHECK(twice.error().contains(u"appears twice"_s));

    // A long first clip that reaches past its neighbour into the third clip.
    QJsonObject longFirst = good;
    clips = longFirst.value(u"clips"_s).toArray();
    QJsonObject first = clips[0].toObject();
    first.insert(u"out"_s, 250);
    clips[0] = first;
    longFirst.insert(u"clips"_s, clips);
    const auto overlap = project.loadJson(longFirst);
    REQUIRE_FALSE(overlap);
    CHECK(overlap.error().contains(u"overlap"_s));

    REQUIRE(project.loadJson(good));
    CHECK(project.clips().size() == 3);
}

TEST_CASE("Loading a long timeline is fast", "[project]")
{
    constexpr int kClips = 20000;
    ProjectModel source;
    REQUIRE(source.addMedia(imageItem(u"m1"_s)));
    QJsonObject document = source.toJson();
    QJsonArray clips;
    for (int i = kClips - 1; i >= 0; --i) {  // Reverse order, so loading has to sort.
        clips.append(QJsonObject{
            {u"id"_s, u"c%1"_s.arg(i)},
            {u"mediaId"_s, u"m1"_s},
            {u"trackId"_s, i % 2 == 0 ? u"V1"_s : u"V2"_s},
            {u"position"_s, i * 10},
            {u"in"_s, 0},
            {u"out"_s, 10},
        });
    }
    document.insert(u"clips"_s, clips);

    ProjectModel project;
    QElapsedTimer timer;
    timer.start();
    REQUIRE(project.loadJson(document));
    // The old quadratic loader needed minutes for this; generous for slow debug builds under load.
    CHECK(timer.elapsed() < 10000);
    REQUIRE(project.clips().size() == kClips);
    CHECK(project.clips().first().id == u"c0"_s);
    CHECK(project.clips().last().trackId == u"V2"_s);
}

TEST_CASE("Tracks can be added, removed and muted or hidden", "[project]")
{
    ProjectModel project;
    QStringList added;
    QStringList changed;
    int timelineChanges = 0;
    QObject::connect(&project, &ProjectModel::trackAdded, [&](const QString& id) { added.append(id); });
    QObject::connect(&project, &ProjectModel::trackChanged, [&](const QString& id) { changed.append(id); });
    QObject::connect(&project, &ProjectModel::timelineChanged, [&] { ++timelineChanges; });
    const auto order = [&project] {
        QStringList ids;
        for (const Track& track : project.tracks())
            ids.append(track.id);
        return ids;
    };

    const auto video = project.addTrack(TrackKind::Video);
    REQUIRE(video);
    CHECK(*video == u"V3"_s);
    const auto audio = project.addTrack(TrackKind::Audio);
    REQUIRE(audio);
    CHECK(*audio == u"A3"_s);
    CHECK(order() == QStringList{u"V1"_s, u"V2"_s, u"V3"_s, u"A1"_s, u"A2"_s, u"A3"_s});
    CHECK(project.findTrack(u"V3"_s)->name == u"Video 3"_s);
    CHECK(project.findTrack(u"A3"_s)->kind == TrackKind::Audio);
    CHECK(added == QStringList{u"V3"_s, u"A3"_s});

    REQUIRE(project.addMedia(videoItem(u"m"_s)));
    REQUIRE(project.addClip(clip(u"c"_s, u"m"_s, u"V3"_s, 0, 0, 10)));
    const auto busy = project.removeTrack(u"V3"_s);
    REQUIRE_FALSE(busy);
    CHECK(busy.error().contains(u"1 clip"_s));
    REQUIRE(project.removeClip(u"c"_s));
    const auto removed = project.removeTrack(u"V3"_s);
    REQUIRE(removed);
    CHECK(removed->id == u"V3"_s);
    CHECK_FALSE(project.removeTrack(u"V3"_s));
    CHECK(project.addTrack(TrackKind::Video) == u"V3"_s);

    timelineChanges = 0;
    REQUIRE(project.setTrackFlags(u"A1"_s, true, false));
    CHECK(project.findTrack(u"A1"_s)->muted);
    REQUIRE(project.setTrackFlags(u"A1"_s, true, false));  // No change, no signal.
    REQUIRE(project.setTrackFlags(u"V1"_s, false, true));
    CHECK(project.findTrack(u"V1"_s)->hidden);
    CHECK(changed == QStringList{u"A1"_s, u"V1"_s});
    CHECK(timelineChanges == 2);
    CHECK_FALSE(project.setTrackFlags(u"V9"_s, true, true));

    ProjectModel loaded;
    REQUIRE(loaded.loadJson(project.toJson()));
    CHECK(loaded.toJson() == project.toJson());
}

TEST_CASE("New video tracks go above the existing video tracks", "[project]")
{
    ProjectModel source;
    QJsonObject document = source.toJson();
    document.insert(u"tracks"_s, QJsonArray{
                                     QJsonObject{{u"id"_s, u"A1"_s}, {u"kind"_s, u"audio"_s}},
                                     QJsonObject{{u"id"_s, u"V7"_s}, {u"kind"_s, u"video"_s}},
                                     QJsonObject{{u"id"_s, u"Vx"_s}, {u"kind"_s, u"video"_s}},
                                 });
    ProjectModel project;
    REQUIRE(project.loadJson(document));
    CHECK(project.addTrack(TrackKind::Video) == u"V8"_s);
    CHECK(project.addTrack(TrackKind::Audio) == u"A2"_s);
    QStringList ids;
    for (const Track& track : project.tracks())
        ids.append(track.id);
    CHECK(ids == QStringList{u"A1"_s, u"V7"_s, u"Vx"_s, u"V8"_s, u"A2"_s});

    document.insert(u"tracks"_s, QJsonArray{QJsonObject{{u"id"_s, u"A1"_s}, {u"kind"_s, u"audio"_s}}});
    REQUIRE(project.loadJson(document));
    CHECK(project.addTrack(TrackKind::Video) == u"V1"_s);
    CHECK(project.tracks().first().id == u"V1"_s);
}
