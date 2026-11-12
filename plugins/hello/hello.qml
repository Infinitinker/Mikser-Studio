// "Greeter" panel of the Hello example plugin.
//
// Uses only the plugin panel contract (docs/plugins.md): the context property `mikserstudio` with
// mikserstudio.execute(commandId, params) -> {ok, result} | {ok: false, error: {code, message}},
// mikserstudio.settings and mikserstudio.pluginId.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root

    readonly property string greeting: mikserstudio.settings.greeting !== undefined ? mikserstudio.settings.greeting : "Hello"
    property bool lastCallFailed: false

    function greet() {
        const params = nameField.text.trim().length > 0 ? { name: nameField.text.trim() } : {}
        // Under the plugin's own id, as the host requires: mikserstudio.examples.hello.greet here.
        const reply = mikserstudio.execute(mikserstudio.pluginId + ".greet", params)
        root.lastCallFailed = !reply.ok
        resultLabel.text = reply.ok ? reply.result.message : reply.error.message
    }

    padding: 12

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Who would you like to greet?")
            font.bold: true
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("world")
                selectByMouse: true
                onAccepted: root.greet()
            }

            Button {
                text: root.greeting
                onClicked: root.greet()
            }
        }

        Label {
            id: resultLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 18
            color: root.lastCallFailed ? "#d9534f" : root.palette.windowText
        }

        Item {
            Layout.fillHeight: true
        }

        Label {
            Layout.fillWidth: true
            text: mikserstudio.pluginId
            opacity: 0.5
            font.pixelSize: 10
            horizontalAlignment: Text.AlignRight
        }
    }
}
