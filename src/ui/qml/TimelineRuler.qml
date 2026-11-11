// Time ruler with timecode labels. Click or drag to seek the program monitor.
import QtQuick

Item {
    id: ruler

    readonly property real stepPixels: timeline.rulerStep * timeline.pixelsPerFrame
    readonly property int firstTick: Math.floor(timeline.scrollX / stepPixels)

    clip: true

    Rectangle {
        anchors.fill: parent
        color: theme.ruler
    }

    Repeater {
        model: Math.ceil(ruler.width / ruler.stepPixels) + 2

        Item {
            id: tick

            required property int index
            readonly property real frame: (ruler.firstTick + index) * timeline.rulerStep

            x: frame * timeline.pixelsPerFrame - timeline.scrollX
            height: ruler.height

            Rectangle {
                y: ruler.height - 11
                width: 1
                height: 11
                color: theme.textDisabled
            }

            Repeater {
                model: 4

                Rectangle {
                    required property int index

                    x: (index + 1) * ruler.stepPixels / 5
                    y: ruler.height - 5
                    width: 1
                    height: 5
                    color: theme.borderStrong
                }
            }

            Text {
                x: 5
                y: 5
                text: timeline.timecode(tick.frame)
                color: theme.textMuted
                font.family: theme.monoFontFamily
                font.pixelSize: theme.fontSizeSmall
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme.border
    }

    MouseArea {
        anchors.fill: parent
        enabled: timeline.canSeek
        cursorShape: Qt.SizeHorCursor
        onPressed: mouse => timeline.seek(timeline.frameAtX(mouse.x))
        onPositionChanged: mouse => {
            if (pressed)
                timeline.seek(timeline.frameAtX(Math.max(0, mouse.x)));
        }
    }
}
