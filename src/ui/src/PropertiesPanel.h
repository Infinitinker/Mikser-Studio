// The Properties panel: shows the current clip and edits its position, in and out through commands.
#pragma once

#include "mikserstudio/core/Project.h"

#include <QWidget>

class QLabel;
class QSpinBox;
class QStackedLayout;

namespace mikserstudio::core {
class CommandBus;
}  // namespace mikserstudio::core

namespace mikserstudio::ui {

class ClipSelection;

// "Properties" panel: the current clip's media, track, position, in/out and duration. Editing
// position runs "timeline.move_clip"; editing in or out runs one "timeline.trim_clip". An In at or
// past the Out moves the Out along (keeping the clip's length, up to the media's end) and an Out at
// or before the In moves the In along, so the trim window can slide in one edit. A rejected edit
// shows the command's message and the fields go back to the clip's real values.
class PropertiesPanel final : public QWidget {
    Q_OBJECT

public:
    PropertiesPanel(core::CommandBus& bus, const core::ProjectModel& project, const ClipSelection& selection,
                    QWidget* parent = nullptr);

    [[nodiscard]] QString shownClipId() const { return m_clipId; }
    [[nodiscard]] QString errorText() const;

    void commitPosition();
    void commitIn();  // After the In field was edited.
    void commitOut();  // After the Out field was edited.

private:
    void refresh();
    void showError(const QString& message);
    void commitTrim(core::FramePos in, core::FramePos out);

    core::CommandBus& m_bus;
    const core::ProjectModel& m_project;
    const ClipSelection& m_selection;
    QString m_clipId;

    QStackedLayout* m_stack = nullptr;
    QLabel* m_media = nullptr;
    QLabel* m_track = nullptr;
    QSpinBox* m_position = nullptr;
    QLabel* m_positionTimecode = nullptr;
    QSpinBox* m_in = nullptr;
    QSpinBox* m_out = nullptr;
    QLabel* m_trimTimecode = nullptr;
    QLabel* m_duration = nullptr;
    QLabel* m_error = nullptr;
};

}  // namespace mikserstudio::ui
