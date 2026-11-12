#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

// Answers by file suffix: .mp4 is 10 s of video with sound, .wav 4 s of audio, .png a still;
// files named "broken.*" cannot be read.
class FakeProbe final : public MediaProbe {
public:
    std::expected<MediaInfo, QString> probe(const QString& path, Rational rate) override
    {
        probedPaths.append(path);
        const QFileInfo file(path);
        if (file.baseName() == u"broken")
            return std::unexpected(u"unsupported codec"_s);
        const auto frames = [rate](double seconds) { return static_cast<FramePos>(seconds * rate.toDouble()); };
        if (file.suffix() == u"wav")
            return MediaInfo{.kind = MediaKind::Audio,
                             .durationFrames = frames(4),
                             .frameRate = {0, 0},
                             .hasAudio = true,
                             .audioSampleRate = 44100,
                             .audioChannels = 2};
        if (file.suffix() == u"png")
            return MediaInfo{.kind = MediaKind::Image,
                             .durationFrames = 1,
                             .width = 640,
                             .height = 480,
                             .frameRate = {0, 0},
                             .hasVideo = true};
        return MediaInfo{.kind = MediaKind::Video,
                         .durationFrames = frames(10),
                         .width = 1920,
                         .height = 1080,
                         .frameRate = {30, 1},
                         .hasVideo = true,
                         .hasAudio = true,
                         .audioSampleRate = 44100,
                         .audioChannels = 2};
    }

    QStringList probedPaths;
};

struct Fixture {
    Fixture()
    {
        registerProjectCommands(bus, project, probe);
        for (const QString& name : {u"clip.mp4"_s, u"song.wav"_s, u"still.png"_s, u"broken.mp4"_s}) {
            QFile file(dir.filePath(name));
            REQUIRE(file.open(QIODevice::WriteOnly));
        }
    }

    QString path(const QString& name) const { return QDir::fromNativeSeparators(dir.filePath(name)); }

    QString import(const QString& name)
    {
        const auto result = bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, path(name)}});
        REQUIRE(result);
        return result->value(u"mediaId").toString();
    }

    QString addClip(const QJsonObject& params)
    {
        const auto result = bus.execute(u"timeline.add_clip"_s, params);
        REQUIRE(result);
        return result->value(u"clipId").toString();
    }

    void undo() { REQUIRE(bus.execute(u"edit.undo"_s)); }
    void redo() { REQUIRE(bus.execute(u"edit.redo"_s)); }

    QTemporaryDir dir;
    UndoStack stack;
    CommandBus bus{stack};
    ProjectModel project;
    FakeProbe probe;
};

}  // namespace

TEST_CASE("Project commands are registered with object schemas", "[project][commands]")
{
    Fixture f;
    for (const QString& id : {u"project.import_media"_s, u"project.remove_media"_s, u"project.list"_s,
                              u"timeline.add_clip"_s, u"timeline.remove_clip"_s, u"timeline.move_clip"_s,
                              u"timeline.trim_clip"_s, u"timeline.add_track"_s, u"timeline.set_track_flags"_s}) {
        const CommandSpec* spec = f.bus.spec(id);
        REQUIRE(spec);
        CHECK(spec->paramsSchema.value(u"type").toString() == u"object"_s);
        CHECK(spec->undoable == (id != u"project.list"));
        CHECK(spec->readOnly == (id == u"project.list"));
        CHECK(spec->confirmation.isEmpty());
    }
}

TEST_CASE("project.import_media probes the file and undo removes it", "[project][commands]")
{
    Fixture f;
    const QString id = f.import(u"clip.mp4"_s);
    CHECK(id.startsWith(u"media-"_s));
    REQUIRE(f.project.findMedia(id));
    const MediaItem item = *f.project.findMedia(id);
    CHECK(item.path == f.path(u"clip.mp4"_s));
    CHECK(item.name == u"clip.mp4"_s);
    CHECK(item.info.durationFrames == 300);
    CHECK(f.probe.probedPaths == QStringList{f.path(u"clip.mp4"_s)});
    CHECK(f.stack.undoText() == u"Import \"clip.mp4\""_s);

    f.undo();
    CHECK(f.project.media().isEmpty());
    f.redo();
    REQUIRE(f.project.media().size() == 1);
    CHECK(f.project.media()[0].id == id);
    CHECK(f.project.media()[0].path == item.path);
}

TEST_CASE("project.import_media takes a display name and gives stills 5 seconds", "[project][commands]")
{
    Fixture f;
    const auto named = f.bus.execute(u"project.import_media"_s,
                                     QJsonObject{{u"path"_s, f.path(u"song.wav"_s)}, {u"name"_s, u"Theme"_s}});
    REQUIRE(named);
    CHECK(f.project.findMedia(named->value(u"mediaId").toString())->name == u"Theme"_s);

    const QString still = f.import(u"still.png"_s);
    CHECK(f.project.findMedia(still)->info.durationFrames == 150);
    f.project.reset({25, 1});
    const QString still25 = f.import(u"still.png"_s);
    CHECK(f.project.findMedia(still25)->info.durationFrames == 125);
}

TEST_CASE("project.import_media reports missing and unreadable files", "[project][commands]")
{
    Fixture f;
    const auto missing = f.bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, f.path(u"nope.mp4"_s)}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == CommandErrorCode::Failed);
    CHECK(missing.error().message.contains(u"does not exist"_s));

    const auto folder = f.bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, f.dir.path()}});
    REQUIRE_FALSE(folder);

    const auto broken = f.bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, f.path(u"broken.mp4"_s)}});
    REQUIRE_FALSE(broken);
    CHECK(broken.error().message.contains(u"unsupported codec"_s));

#ifdef Q_OS_WIN
    // Network shares are refused before the file is touched (project files cannot hold them).
    const auto share = f.bus.execute(u"project.import_media"_s,
                                     QJsonObject{{u"path"_s, u"//server.invalid/share/clip.mp4"_s}});
    REQUIRE_FALSE(share);
    CHECK(share.error().message.contains(u"network share"_s));
    CHECK_FALSE(f.probe.probedPaths.contains(u"//server.invalid/share/clip.mp4"_s));
#endif

    const auto empty = f.bus.execute(u"project.import_media"_s, QJsonObject{{u"path"_s, u""_s}});
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == CommandErrorCode::InvalidParams);
    CHECK(f.project.media().isEmpty());
    CHECK_FALSE(f.stack.canUndo());
}

TEST_CASE("project.remove_media and its undo restore the bin order", "[project][commands]")
{
    Fixture f;
    const QString first = f.import(u"clip.mp4"_s);
    const QString second = f.import(u"song.wav"_s);
    const QString third = f.import(u"still.png"_s);
    const MediaItem original = *f.project.findMedia(second);

    REQUIRE(f.bus.execute(u"project.remove_media"_s, QJsonObject{{u"mediaId"_s, second}}));
    CHECK(f.project.findMedia(second) == nullptr);
    CHECK(f.stack.undoText() == u"Remove \"song.wav\""_s);

    f.undo();
    REQUIRE(f.project.media().size() == 3);
    CHECK(f.project.media()[0].id == first);
    CHECK(f.project.media()[1].id == second);
    CHECK(f.project.media()[2].id == third);
    CHECK(f.project.media()[1].path == original.path);
    CHECK(f.project.media()[1].info.audioSampleRate == original.info.audioSampleRate);
    f.redo();
    CHECK(f.project.findMedia(second) == nullptr);

    const auto unknown = f.bus.execute(u"project.remove_media"_s, QJsonObject{{u"mediaId"_s, u"media-x"_s}});
    REQUIRE_FALSE(unknown);

    f.addClip(QJsonObject{{u"mediaId"_s, first}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 0}});
    const auto inUse = f.bus.execute(u"project.remove_media"_s, QJsonObject{{u"mediaId"_s, first}});
    REQUIRE_FALSE(inUse);
    CHECK(inUse.error().message.contains(u"Remove those clips first"_s));
}

TEST_CASE("project.list returns the project JSON", "[project][commands]")
{
    Fixture f;
    const QString id = f.import(u"clip.mp4"_s);
    const auto listed = f.bus.execute(u"project.list"_s);
    REQUIRE(listed);
    CHECK(*listed == f.project.toJson());
    CHECK(listed->value(u"media").toArray()[0].toObject().value(u"id").toString() == id);
    CHECK(f.stack.undoText() == u"Import \"clip.mp4\""_s);
}

TEST_CASE("timeline.add_clip defaults in/out to the whole media", "[project][commands]")
{
    Fixture f;
    const QString video = f.import(u"clip.mp4"_s);
    const QString still = f.import(u"still.png"_s);

    const QString full = f.addClip(QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 30}});
    const Clip* clip = f.project.findClip(full);
    REQUIRE(clip);
    CHECK(clip->id.startsWith(u"clip-"_s));
    CHECK(clip->position == 30);
    CHECK(clip->in == 0);
    CHECK(clip->out == 300);

    const QString partial = f.addClip(
        QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 0}, {u"in"_s, 100}});
    CHECK(f.project.findClip(partial)->in == 100);
    CHECK(f.project.findClip(partial)->out == 300);

    const QString image = f.addClip(QJsonObject{{u"mediaId"_s, still}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 500}});
    CHECK(f.project.findClip(image)->length() == 150);

    const auto overlap = f.bus.execute(u"timeline.add_clip"_s,
                                       QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 0}});
    REQUIRE_FALSE(overlap);
    CHECK(overlap.error().code == CommandErrorCode::Failed);
    const auto badParams = f.bus.execute(
        u"timeline.add_clip"_s, QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, -3}});
    REQUIRE_FALSE(badParams);
    CHECK(badParams.error().code == CommandErrorCode::InvalidParams);
    const auto unknownMedia = f.bus.execute(
        u"timeline.add_clip"_s, QJsonObject{{u"mediaId"_s, u"media-x"_s}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 0}});
    REQUIRE_FALSE(unknownMedia);

    f.undo();
    CHECK(f.project.findClip(image) == nullptr);
    f.redo();
    REQUIRE(f.project.findClip(image));
    CHECK(f.project.findClip(image)->position == 500);
}

TEST_CASE("timeline.remove_clip undo restores the exact clip", "[project][commands]")
{
    Fixture f;
    const QString video = f.import(u"clip.mp4"_s);
    const QString id = f.addClip(
        QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 12}, {u"in"_s, 7}, {u"out"_s, 99}});
    const Clip original = *f.project.findClip(id);

    REQUIRE(f.bus.execute(u"timeline.remove_clip"_s, QJsonObject{{u"clipId"_s, id}}));
    CHECK(f.project.clips().isEmpty());
    CHECK(f.stack.undoText() == u"Remove Clip"_s);
    f.undo();
    const Clip* restored = f.project.findClip(id);
    REQUIRE(restored);
    CHECK(restored->mediaId == original.mediaId);
    CHECK(restored->trackId == original.trackId);
    CHECK(restored->position == original.position);
    CHECK(restored->in == original.in);
    CHECK(restored->out == original.out);
    f.redo();
    CHECK(f.project.findClip(id) == nullptr);

    CHECK_FALSE(f.bus.execute(u"timeline.remove_clip"_s, QJsonObject{{u"clipId"_s, id}}));
}

TEST_CASE("timeline.move_clip and timeline.trim_clip are undoable", "[project][commands]")
{
    Fixture f;
    const QString video = f.import(u"clip.mp4"_s);
    const QString id = f.addClip(
        QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 0}, {u"out"_s, 100}});
    f.addClip(QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 400}});

    REQUIRE(f.bus.execute(u"timeline.move_clip"_s,
                          QJsonObject{{u"clipId"_s, id}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 40}}));
    CHECK(f.project.findClip(id)->trackId == u"V2"_s);
    CHECK(f.project.findClip(id)->position == 40);
    CHECK(f.stack.undoText() == u"Move Clip"_s);
    f.undo();
    CHECK(f.project.findClip(id)->trackId == u"V1"_s);
    CHECK(f.project.findClip(id)->position == 0);
    f.redo();
    CHECK(f.project.findClip(id)->position == 40);
    f.undo();

    const auto blocked = f.bus.execute(u"timeline.move_clip"_s,
                                       QJsonObject{{u"clipId"_s, id}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 350}});
    REQUIRE_FALSE(blocked);
    CHECK(f.project.findClip(id)->position == 0);

    REQUIRE(f.bus.execute(u"timeline.trim_clip"_s, QJsonObject{{u"clipId"_s, id}, {u"in"_s, 20}, {u"out"_s, 80}}));
    CHECK(f.project.findClip(id)->in == 20);
    CHECK(f.project.findClip(id)->out == 80);
    CHECK(f.project.findClip(id)->position == 0);
    CHECK(f.stack.undoText() == u"Trim Clip"_s);
    f.undo();
    CHECK(f.project.findClip(id)->in == 0);
    CHECK(f.project.findClip(id)->out == 100);
    f.redo();
    CHECK(f.project.findClip(id)->out == 80);

    const auto tooLong = f.bus.execute(u"timeline.trim_clip"_s,
                                       QJsonObject{{u"clipId"_s, id}, {u"in"_s, 0}, {u"out"_s, 301}});
    REQUIRE_FALSE(tooLong);
    const auto missing = f.bus.execute(u"timeline.trim_clip"_s, QJsonObject{{u"clipId"_s, id}, {u"in"_s, 0}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == CommandErrorCode::InvalidParams);
}

TEST_CASE("A whole editing session undoes back to an empty project", "[project][commands]")
{
    Fixture f;
    const QJsonObject empty = f.project.toJson();
    const QString video = f.import(u"clip.mp4"_s);
    const QString audio = f.import(u"song.wav"_s);
    const QString clip = f.addClip(QJsonObject{{u"mediaId"_s, video}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 0}});
    f.addClip(QJsonObject{{u"mediaId"_s, audio}, {u"trackId"_s, u"A1"_s}, {u"position"_s, 0}});
    REQUIRE(f.bus.execute(u"timeline.move_clip"_s,
                          QJsonObject{{u"clipId"_s, clip}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 10}}));
    REQUIRE(f.bus.execute(u"timeline.trim_clip"_s, QJsonObject{{u"clipId"_s, clip}, {u"in"_s, 1}, {u"out"_s, 2}}));
    REQUIRE(f.bus.execute(u"timeline.remove_clip"_s, QJsonObject{{u"clipId"_s, clip}}));
    const QJsonObject edited = f.project.toJson();

    while (f.stack.canUndo())
        f.undo();
    CHECK(f.project.toJson() == empty);
    while (f.stack.canRedo())
        f.redo();
    CHECK(f.project.toJson() == edited);
}

TEST_CASE("Frame params beyond the supported range are rejected, not turned into 0", "[project][commands]")
{
    Fixture f;
    const QString media = f.import(u"still.png"_s);
    const QString clip = f.addClip(QJsonObject{{u"mediaId"_s, media}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 10}});
    const auto invalid = [&f](const QString& id, const QJsonObject& params) {
        const auto result = f.bus.execute(id, params);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == CommandErrorCode::InvalidParams);
    };

    invalid(u"timeline.add_clip"_s, QJsonObject{{u"mediaId"_s, media}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 1e19}});
    invalid(u"timeline.add_clip"_s,
            QJsonObject{{u"mediaId"_s, media}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 0}, {u"in"_s, 1e19}});
    invalid(u"timeline.move_clip"_s, QJsonObject{{u"clipId"_s, clip}, {u"trackId"_s, u"V1"_s}, {u"position"_s, 1e19}});
    invalid(u"timeline.trim_clip"_s, QJsonObject{{u"clipId"_s, clip}, {u"in"_s, 1e19}, {u"out"_s, 2e19}});
    invalid(u"timeline.move_clip"_s,
            QJsonObject{{u"clipId"_s, clip}, {u"trackId"_s, u"V1"_s}, {u"position"_s, qint64{kMaxFrame} + 1}});
    CHECK(f.project.findClip(clip)->position == 10);
    CHECK(f.project.clips().size() == 1);

    // An image clip whose default out (in + 5 s) would pass the cap fails with the model's message.
    const auto pastCap = f.bus.execute(
        u"timeline.add_clip"_s,
        QJsonObject{{u"mediaId"_s, media}, {u"trackId"_s, u"V2"_s}, {u"position"_s, 0}, {u"in"_s, qint64{kMaxFrame}}});
    REQUIRE_FALSE(pastCap);
    CHECK(pastCap.error().code == CommandErrorCode::Failed);
}

TEST_CASE("project.import_media stores provenance in the same undo step", "[project][commands]")
{
    Fixture f;
    const QJsonObject provenance = json(R"({"workflow": "upscale", "seed": 7})");
    const auto imported = f.bus.execute(u"project.import_media"_s,
                                        QJsonObject{{u"path"_s, f.path(u"clip.mp4"_s)}, {u"provenance"_s, provenance}});
    REQUIRE(imported);
    const QString id = imported->value(u"mediaId").toString();
    CHECK(f.project.findMedia(id)->provenance == provenance);

    f.undo();
    CHECK(f.project.media().isEmpty());
    f.redo();
    REQUIRE(f.project.findMedia(id));
    CHECK(f.project.findMedia(id)->provenance == provenance);

    CHECK_FALSE(f.bus.execute(u"project.import_media"_s,
                              QJsonObject{{u"path"_s, f.path(u"clip.mp4"_s)}, {u"provenance"_s, u"seed 7"_s}}));
}

TEST_CASE("timeline.add_track adds the next track and undo removes it", "[project][commands]")
{
    Fixture f;
    const auto added = f.bus.execute(u"timeline.add_track"_s, json(R"({"kind": "video"})"));
    REQUIRE(added);
    CHECK(added->value(u"trackId").toString() == u"V3"_s);
    CHECK(f.project.tracks()[2].id == u"V3"_s);
    CHECK(f.stack.undoText() == u"Add Video Track"_s);

    const auto audio = f.bus.execute(u"timeline.add_track"_s, json(R"({"kind": "audio"})"));
    REQUIRE(audio);
    CHECK(audio->value(u"trackId").toString() == u"A3"_s);
    CHECK(f.project.tracks().last().id == u"A3"_s);

    f.undo();
    f.undo();
    CHECK(f.project.tracks().size() == 4);
    CHECK_FALSE(f.project.findTrack(u"V3"_s));
    f.redo();
    f.redo();
    CHECK(f.project.findTrack(u"V3"_s));
    CHECK(f.project.findTrack(u"A3"_s));

    const auto bad = f.bus.execute(u"timeline.add_track"_s, json(R"({"kind": "subtitle"})"));
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == CommandErrorCode::InvalidParams);
}

TEST_CASE("timeline.set_track_flags mutes and hides tracks undoably", "[project][commands]")
{
    Fixture f;
    REQUIRE(f.bus.execute(u"timeline.set_track_flags"_s, json(R"({"trackId": "A1", "muted": true})")));
    CHECK(f.project.findTrack(u"A1"_s)->muted);
    CHECK_FALSE(f.project.findTrack(u"A1"_s)->hidden);
    CHECK(f.stack.undoText() == u"Mute Track A1"_s);

    REQUIRE(f.bus.execute(u"timeline.set_track_flags"_s, json(R"({"trackId": "V2", "hidden": true})")));
    CHECK(f.stack.undoText() == u"Hide Track V2"_s);
    REQUIRE(f.bus.execute(u"timeline.set_track_flags"_s, json(R"({"trackId": "V2", "muted": true, "hidden": false})")));
    CHECK(f.stack.undoText() == u"Change Track V2"_s);

    // Nothing changes: no undo step.
    REQUIRE(f.bus.execute(u"timeline.set_track_flags"_s, json(R"({"trackId": "V2"})")));
    CHECK(f.stack.undoText() == u"Change Track V2"_s);

    f.undo();
    CHECK(f.project.findTrack(u"V2"_s)->hidden);
    CHECK_FALSE(f.project.findTrack(u"V2"_s)->muted);
    f.undo();
    f.undo();
    CHECK_FALSE(f.project.findTrack(u"A1"_s)->muted);
    CHECK_FALSE(f.project.findTrack(u"V2"_s)->hidden);
    f.redo();
    CHECK(f.project.findTrack(u"A1"_s)->muted);

    const auto missing = f.bus.execute(u"timeline.set_track_flags"_s, json(R"({"trackId": "V9", "muted": true})"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == CommandErrorCode::Failed);
}
