pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

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
        color: "#ff171e2a"
        radius: 10
        border.color: "#40516a"
    }

    contentItem: Column {
        spacing: 12

        Label {
            text: qsTr("Keyboard shortcuts")
            color: "#f3f6fb"
            font.pixelSize: 20
            font.weight: Font.DemiBold
        }
        Label {
            text: control.playerPreset ? qsTr("Player preset") : qsTr("Review preset")
            color: "#9fc3ff"
            font.pixelSize: 12
        }
        Label {
            width: parent.width
            text: control.playerPreset ? qsTr("Left / Right     Seek 5 seconds\nCtrl+Left / Right  Seek 30 seconds\n, / .              Previous / next frame\nSpace              Play / pause\nI / O              Set In / Out\n\\                  Play selected range\nF11 or double-click Full screen\nRight click         Viewer commands\nTab                 Hide interface\n?                   This help") : qsTr("Left / Right      Previous / next frame\nShift+Left / Right Step 5 frames\nCtrl+Left / Right  Step 1 second\nA / D              Previous / next frame\nSpace              Play / pause\nI / O              Set In / Out\n\\                  Play selected range\nF11 or double-click Full screen\nRight click         Viewer commands\nTab                 Hide interface\n?                   This help")
            color: "#d8e2f2"
            font.family: "Consolas"
            font.pixelSize: 13
            lineHeight: 1.3
        }
    }
}
