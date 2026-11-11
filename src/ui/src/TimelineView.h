#pragma once

#include <QQuickWidget>

namespace mikserstudio::ui {

class ThemeObject;
class TimelineController;

// The timeline panel: a QQuickWidget showing qrc:/mikserstudio/ui/qml/Timeline.qml with the
// controller as context property "timeline" and the theme as "theme".
//
// Input handled here rather than in QML: media drops from the bin (kMediaIdMimeType), the
// wheel (scrolls time; Ctrl+wheel zooms around the cursor; Shift+wheel scrolls the tracks) and
// keys (Delete/Backspace removes the selection, '\' zooms to fit, S toggles snapping and says so
// in a toast, Home/End seek).
class TimelineView final : public QQuickWidget {
    Q_OBJECT

public:
    TimelineView(TimelineController& controller, ThemeObject& theme, QWidget* parent = nullptr);

    [[nodiscard]] TimelineController& controller() const { return m_controller; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QPointF toTrackArea(QPointF position) const;

    TimelineController& m_controller;
};

}  // namespace mikserstudio::ui
