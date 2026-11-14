// The timeline panel. Context properties: `timeline` (TimelineController) and `theme`
// (ThemeObject). Drops, the wheel and keys are handled by the C++ view (TimelineView).
import QtQuick

Rectangle {
    id: root

    readonly property real areaX: timeline.headerWidth
    readonly property real areaY: timeline.rulerHeight
    readonly property int trackCount: timeline.tracks.length

    color: theme.panel

    Binding {
        target: timeline
        property: "viewportWidth"
        value: Math.max(1, root.width - root.areaX)
    }

    Binding {
        target: timeline
        property: "viewportHeight"
        value: Math.max(1, root.height - root.areaY)
    }

    // Track lanes, clips, drop preview and snap guide. The rows scroll vertically by scrollY.
    Item {
        id: trackArea

        x: root.areaX
        y: root.areaY
        width: root.width - root.areaX
        height: root.height - root.areaY
        clip: true

        // Clicking empty space clears the selection.
        MouseArea {
            anchors.fill: parent
            onPressed: timeline.clearSelection()
        }

        Item {
            id: rows

            y: -timeline.scrollY
            width: trackArea.width
            height: root.trackCount * timeline.trackHeight

            Repeater {
                model: timeline.tracks

                Rectangle {
                    required property var modelData
                    required property int index

                    readonly property bool off: modelData.kind === "video" ? modelData.hidden : modelData.muted

                    y: index * timeline.trackHeight
                    width: trackArea.width
                    height: timeline.trackHeight
                    color: modelData.kind === "video" ? theme.trackVideo : theme.trackAudio
                    opacity: off ? 0.55 : 1

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: theme.window
                    }
                }
            }

            Rectangle {
                id: dropGhost

                visible: timeline.dropActive
                x: timeline.dropFrame * timeline.pixelsPerFrame - timeline.scrollX
                y: timeline.dropRow * timeline.trackHeight + 3
                width: Math.max(2, timeline.dropLength * timeline.pixelsPerFrame)
                height: timeline.trackHeight - 7
                radius: theme.radiusMd
                color: "transparent"
                border.width: 2
                border.color: timeline.dropValid ? theme.accent : theme.danger
                opacity: 0.9

                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: timeline.dropValid ? theme.accent : theme.danger
                    opacity: 0.18
                }
            }

            Repeater {
                id: clips

                model: timeline.model
                delegate: TimelineClip {}
            }

            Rectangle {
                visible: timeline.snapGuide >= 0
                x: timeline.snapGuide * timeline.pixelsPerFrame - timeline.scrollX
                width: 1
                height: rows.height
                color: theme.warning
                z: 20
            }

            Text {
                visible: clips.count === 0 && !timeline.dropActive
                x: 24
                y: Math.min(root.trackCount, 2) * timeline.trackHeight - height / 2
                text: qsTr("Drag media items from the media bin here, or double-click one to add it at the end.")
                color: theme.textMuted
                font.family: theme.fontFamily
                font.pixelSize: theme.fontSize
            }
        }

        // Vertical scroll position, shown while the rows do not fit (Shift+wheel scrolls them).
        Rectangle {
            visible: rows.height > trackArea.height
            anchors.right: parent.right
            anchors.rightMargin: 2
            y: trackArea.height * timeline.scrollY / rows.height
            width: 4
            height: Math.max(16, trackArea.height * trackArea.height / rows.height)
            radius: 2
            color: theme.borderStrong
            z: 30
        }
    }

    // Track headers: name, kind and the hide (video) and mute toggles ("timeline.set_track_flags").
    Rectangle {
        y: root.areaY
        width: root.areaX
        height: root.height - root.areaY
        color: theme.panelAlt
        clip: true

        Repeater {
            model: timeline.tracks

            Item {
                id: header

                required property var modelData
                required property int index

                readonly property bool video: modelData.kind === "video"

                y: index * timeline.trackHeight - timeline.scrollY
                width: root.areaX
                height: timeline.trackHeight

                Rectangle {
                    x: 0
                    y: 6
                    width: 3
                    height: parent.height - 12
                    radius: 1.5
                    color: header.video ? theme.clipVideo : theme.clipAudio
                }

                Text {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -7
                    text: header.modelData.id
                    color: theme.text
                    font.family: theme.fontFamily
                    font.pixelSize: theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }

                Text {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 10
                    text: header.video ? qsTr("Video") : qsTr("Audio")
                    color: theme.textMuted
                    font.family: theme.fontFamily
                    font.pixelSize: theme.fontSizeSmall
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    TrackToggle {
                        visible: header.video
                        eye: true
                        active: header.modelData.hidden
                        activeColor: theme.warning
                        tip: header.modelData.hidden ? qsTr("Show track %1").arg(header.modelData.id)
                                                     : qsTr("Hide track %1").arg(header.modelData.id)
                        onToggled: timeline.setTrackHidden(header.modelData.id, !header.modelData.hidden)
                    }

                    TrackToggle {
                        label: "M"
                        active: header.modelData.muted
                        activeColor: theme.warning
                        tip: header.modelData.muted ? qsTr("Unmute track %1").arg(header.modelData.id)
                                                    : qsTr("Mute track %1").arg(header.modelData.id)
                        onToggled: timeline.setTrackMuted(header.modelData.id, !header.modelData.muted)
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme.window
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: theme.border
        }
    }

    TimelineRuler {
        x: root.areaX
        width: root.width - root.areaX
        height: root.areaY
    }

    // Snapping state at the right end of the ruler; click it or press S to toggle.
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: 8
        y: (root.areaY - height) / 2
        width: snapRow.implicitWidth + 16
        height: 20
        radius: height / 2
        z: 55
        // Opaque, so the ruler labels underneath do not show through.
        color: timeline.snapping ? Qt.tint(theme.ruler, Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.25))
                                 : (snapArea.containsMouse ? theme.hover : theme.surface)
        border.width: 1
        border.color: timeline.snapping ? theme.accent : theme.borderStrong

        Row {
            id: snapRow

            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: timeline.snapping ? theme.accent : "transparent"
                border.width: 1.5
                border.color: timeline.snapping ? theme.accent : theme.textMuted
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: timeline.snapping ? qsTr("Snap on") : qsTr("Snap off")
                color: timeline.snapping ? theme.text : theme.textMuted
                font.family: theme.fontFamily
                font.pixelSize: theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: snapArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: timeline.snapping = !timeline.snapping
        }
    }

    // Corner: the playhead timecode.
    Rectangle {
        width: root.areaX
        height: root.areaY
        color: theme.ruler

        Text {
            anchors.centerIn: parent
            text: timeline.timecode(timeline.playhead)
            color: theme.accentHover
            font.family: theme.monoFontFamily
            font.pixelSize: theme.fontSize
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: theme.border
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: theme.border
        }
    }

    // Playhead.
    Item {
        id: playhead

        readonly property real px: root.areaX + timeline.playhead * timeline.pixelsPerFrame - timeline.scrollX

        x: px
        z: 50
        visible: px >= root.areaX - 1 && px <= root.width + 1

        Rectangle {
            x: -1
            y: root.areaY - 8
            width: 2
            height: root.height - y
            color: theme.playhead
        }

        Rectangle {
            x: -6
            y: root.areaY - 16
            width: 12
            height: 10
            radius: 2
            color: theme.playhead
        }

        Rectangle {
            x: -4
            y: root.areaY - 11
            width: 8
            height: 8
            rotation: 45
            color: theme.playhead
        }
    }

    // Errors of timeline actions and snapping changes, shown briefly every time they happen.
    Rectangle {
        id: toast

        property bool error: false

        anchors.horizontalCenter: trackArea.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: Math.min(toastText.implicitWidth + 28, trackArea.width - 24)
        height: toastText.implicitHeight + 14
        radius: theme.radiusMd
        color: theme.surface
        border.color: error ? theme.danger : theme.accent
        opacity: 0
        visible: opacity > 0
        z: 60

        Text {
            id: toastText

            anchors.centerIn: parent
            width: parent.width - 28
            color: theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.family: theme.fontFamily
            font.pixelSize: theme.fontSize
        }

        Behavior on opacity {
            NumberAnimation {
                duration: 160
            }
        }

        Timer {
            id: toastTimer

            onTriggered: toast.opacity = 0
        }

        Connections {
            target: timeline

            function onToastRequested(text, error) {
                toastText.text = text;
                toast.error = error;
                toast.opacity = 1;
                toastTimer.interval = error ? 3500 : 1600;
                toastTimer.restart();
            }
        }
    }
}
