// The placement rules of the project as seen by the UI, to pick a track and a frame before running a
// command.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QString>

namespace mikserstudio::ui {

// The UI-side mirror of the ProjectModel placement rules, used to pick targets before running a
// command (the command still validates everything).

// Video tracks take video and images; audio tracks take media with audio.
[[nodiscard]] bool trackAccepts(const core::Track& track, const core::MediaItem& media);
// The first track (in project order) that accepts `media`, or empty.
[[nodiscard]] QString firstTrackFor(const core::ProjectModel& project, const core::MediaItem& media);
// The frame after the last clip on the track (0 when it is empty).
[[nodiscard]] core::FramePos trackEnd(const core::ProjectModel& project, const QString& trackId);

}  // namespace mikserstudio::ui
