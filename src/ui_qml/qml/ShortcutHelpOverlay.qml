pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "VcsTheme.js" as Theme

Popup {
    id: control

    property bool playerPreset: false

    objectName: "shortcutHelpOverlay"
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, parent.width - 48)
    modal: true
    dim: true
    focus: true
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.menu
        radius: 10
        border.color: Theme.menuBorder
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: qsTr("Keyboard shortcuts")
            color: Theme.primaryText
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Label {
            text: control.playerPreset ? qsTr("Player shortcuts") : qsTr("Frame review shortcuts")
            color: "#9fc3ff"
            font.pixelSize: 12
        }
        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 22
            rowSpacing: 7

            Repeater {
                model: control.playerPreset ? [[qsTr("Left / Right"), qsTr("Seek 5 seconds")], [qsTr("Ctrl+Left / Right"), qsTr("Seek 30 seconds")], [qsTr(", / ."), qsTr("Previous / next frame")], [qsTr("Space"), qsTr("Play / pause")], [qsTr("I / O"), qsTr("Set In / Out")], [qsTr("\\"), qsTr("Play selected range")], [qsTr("F11 or double-click"), qsTr("Full screen")], [qsTr("Right click"), qsTr("Viewer commands")], [qsTr("Tab"), qsTr("Hide interface")], [qsTr("?"), qsTr("This help")]] : [[qsTr("Left / Right"), qsTr("Previous / next frame")], [qsTr("Shift+Left / Right"), qsTr("Step 5 frames")], [qsTr("Ctrl+Left / Right"), qsTr("Step 1 second")], [qsTr("A / D"), qsTr("Previous / next frame")], [qsTr("Space"), qsTr("Play / pause")], [qsTr("I / O"), qsTr("Set In / Out")], [qsTr("\\"), qsTr("Play selected range")], [qsTr("F11 or double-click"), qsTr("Full screen")], [qsTr("Right click"), qsTr("Viewer commands")], [qsTr("Tab"), qsTr("Hide interface")], [qsTr("?"), qsTr("This help")]]

                delegate: RowLayout {
                    required property var modelData
                    Layout.columnSpan: 2
                    Layout.fillWidth: true

                    Label {
                        Layout.preferredWidth: 180
                        text: String(parent.modelData[0])
                        color: "#9fc3ff"
                        font.pixelSize: 13
                    }
                    Label {
                        Layout.fillWidth: true
                        text: String(parent.modelData[1])
                        color: "#d8e2f2"
                        font.pixelSize: 13
                    }
                }
            }
        }
    }
}
