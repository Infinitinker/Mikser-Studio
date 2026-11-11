// A small on/off button in a track header or the timeline corner. Shows `label` (or the eye
// glyph when `eye` is set), lit with `activeColor` while `active`. Emits toggled() on click.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: toggle

    property bool active: false
    property string label: ""
    property bool eye: false
    property string tip: ""
    property color activeColor: theme.accent

    signal toggled

    implicitWidth: eye ? 22 : Math.max(20, labelText.implicitWidth + 10)
    implicitHeight: 18
    radius: theme.radiusSm
    color: active ? activeColor : (area.containsMouse ? theme.hover : "transparent")
    border.width: 1
    border.color: active ? activeColor : theme.borderStrong

    Text {
        id: labelText

        visible: !toggle.eye
        anchors.centerIn: parent
        text: toggle.label
        color: toggle.active ? theme.window : theme.textMuted
        font.family: theme.fontFamily
        font.pixelSize: theme.fontSizeSmall
        font.weight: Font.DemiBold
    }

    // Eye: an almond outline with a pupil; struck through while active (hidden).
    Item {
        visible: toggle.eye
        anchors.centerIn: parent
        width: 12
        height: 8

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: "transparent"
            border.width: 1.5
            border.color: toggle.active ? theme.window : theme.textMuted
        }

        Rectangle {
            anchors.centerIn: parent
            width: 4
            height: 4
            radius: 2
            color: toggle.active ? theme.window : theme.textMuted
        }

        Rectangle {
            visible: toggle.active
            anchors.centerIn: parent
            width: 16
            height: 1.5
            rotation: -35
            color: theme.window
        }
    }

    MouseArea {
        id: area

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: toggle.toggled()
    }

    ToolTip {
        visible: area.containsMouse && toggle.tip.length > 0
        delay: 500
        text: toggle.tip

        contentItem: Text {
            text: toggle.tip
            color: theme.text
            font.family: theme.fontFamily
            font.pixelSize: theme.fontSizeSmall
        }

        background: Rectangle {
            color: theme.surface
            border.color: theme.border
            radius: theme.radiusSm
        }
    }
}
