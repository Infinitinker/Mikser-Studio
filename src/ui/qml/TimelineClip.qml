// One clip on the timeline. Drag horizontally to move it (with snapping) or vertically to another
// track of the same kind; the move runs "timeline.move_clip" when the mouse is released. A plain
// click (no drag, no Ctrl/Shift) leaves this clip as the only selected one. The parent is the
// rows item of Timeline.qml, which scrolls by timeline.scrollY.
import QtQuick

Item {
    id: clip

    required property string clipId
    required property string name
    required property int trackRow
    required property real position
    required property real length
    required property string kind
    required property color clipColor
    required property bool selected
    required property bool generated
    required property bool trackOff

    property bool dragging: false
    property real dragFrame: position
    property int dragRow: trackRow

    readonly property real shownFrame: dragging ? dragFrame : position
    readonly property int shownRow: dragging ? dragRow : trackRow

    x: shownFrame * timeline.pixelsPerFrame - timeline.scrollX
    y: shownRow * timeline.trackHeight + 3
    width: Math.max(2, length * timeline.pixelsPerFrame)
    height: timeline.trackHeight - 7
    z: dragging ? 10 : (selected ? 2 : 1)

    Rectangle {
        id: body

        anchors.fill: parent
        radius: theme.radiusMd
        color: clip.selected ? Qt.lighter(clip.clipColor, 1.18) : clip.clipColor
        border.width: clip.selected ? 2 : 1
        border.color: clip.selected ? theme.text : Qt.darker(clip.clipColor, 1.35)
        opacity: clip.dragging ? 0.85 : (clip.trackOff ? 0.45 : 1)

        // Soft highlight along the top edge.
        Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: Math.min(parent.height / 2, 14)
            radius: parent.radius
            color: "white"
            opacity: 0.07
        }

        Text {
            id: label

            x: 8
            y: 6
            width: parent.width - 16 - (badge.visible ? badge.width + 6 : 0)
            text: clip.name
            color: "white"
            elide: Text.ElideRight
            font.family: theme.fontFamily
            font.pixelSize: theme.fontSize
            font.weight: Font.DemiBold
            visible: parent.width > 24
        }

        Text {
            x: 8
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5
            width: parent.width - 16
            text: timeline.timecode(clip.length)
            color: "white"
            opacity: 0.7
            elide: Text.ElideRight
            font.family: theme.monoFontFamily
            font.pixelSize: theme.fontSizeSmall - 1
            visible: parent.width > 70
        }

        Rectangle {
            id: badge

            visible: clip.generated && parent.width > 60
            anchors.right: parent.right
            anchors.rightMargin: 6
            y: 6
            width: badgeText.implicitWidth + 8
            height: badgeText.implicitHeight + 2
            radius: 3
            color: theme.warning

            Text {
                id: badgeText

                anchors.centerIn: parent
                text: "AI"
                color: theme.window
                font.family: theme.fontFamily
                font.pixelSize: theme.fontSizeSmall - 1
                font.weight: Font.Bold
            }
        }
    }

    MouseArea {
        id: mouseArea

        property real pressX: 0
        property real pressY: 0
        property bool pressAdditive: false

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: clip.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

        onPressed: mouse => {
            const additive = (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0;
            pressAdditive = additive;
            // A press on a selected clip keeps the selection until release: it may start a drag.
            if (!clip.selected || additive)
                timeline.select(clip.clipId, additive);
            const point = mapToItem(clip.parent, mouse.x, mouse.y);
            pressX = point.x;
            pressY = point.y;
        }
        onPositionChanged: mouse => {
            if (!pressed)
                return;
            const point = mapToItem(clip.parent, mouse.x, mouse.y);
            const dx = point.x - pressX;
            if (!clip.dragging && Math.abs(dx) < 3 && Math.abs(point.y - pressY) < 3)
                return;
            clip.dragging = true;
            const rawFrame = Math.round(clip.position + dx / timeline.pixelsPerFrame);
            clip.dragFrame = timeline.snapPosition(clip.clipId, rawFrame);
            const row = timeline.rowAtY(point.y - timeline.scrollY);
            clip.dragRow = row >= 0 && timeline.trackKind(row) === timeline.trackKind(clip.trackRow) ? row : clip.trackRow;
        }
        onReleased: {
            if (clip.dragging)
                timeline.moveClip(clip.clipId, timeline.trackForMove(clip.clipId, clip.dragRow), clip.dragFrame);
            else if (!pressAdditive)
                timeline.select(clip.clipId, false);
            clip.dragging = false;
            timeline.clearSnapGuide();
        }
        onCanceled: {
            clip.dragging = false;
            timeline.clearSnapGuide();
        }
    }
}
