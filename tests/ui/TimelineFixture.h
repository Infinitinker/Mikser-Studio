#pragma once

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/ClipSelection.h"

#include "TimelineController.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>

namespace mikserstudio::ui::test {

// Imports are not tested here: media is added to the model directly.
class NoMediaProbe final : public core::MediaProbe {
public:
    [[nodiscard]] std::expected<core::MediaInfo, QString> probe(const QString&, core::Rational) override
    {
        return std::unexpected(QStringLiteral("The tests do not probe media."));
    }
};

// A project with the project/timeline commands, one 300-frame video ("media-video") and the
// timeline controller the timeline panel uses.
struct TimelineFixture {
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::ProjectModel project;
    NoMediaProbe probe;
    ClipSelection selection{project};
    TimelineController timeline{bus, project, selection};

    TimelineFixture()
    {
        core::registerProjectCommands(bus, project, probe);
        core::MediaItem media;
        media.id = QStringLiteral("media-video");
        media.path = QDir::rootPath() + QStringLiteral("media/Interview.mp4");
        media.info.kind = core::MediaKind::Video;
        media.info.durationFrames = 300;
        media.info.hasVideo = true;
        media.info.hasAudio = true;
        REQUIRE(project.addMedia(media));
    }

    void addClip(const QString& id, const QString& trackId, core::FramePos position, core::FramePos in,
                 core::FramePos out)
    {
        REQUIRE(project.addClip(core::Clip{.id = id,
                                           .mediaId = QStringLiteral("media-video"),
                                           .trackId = trackId,
                                           .position = position,
                                           .in = in,
                                           .out = out}));
    }
};

}  // namespace mikserstudio::ui::test
