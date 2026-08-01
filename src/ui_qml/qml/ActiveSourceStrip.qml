pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: control

    required property var sourcesModel
    required property int sourceCount
    required property int canonicalSourceIndex
    required property bool busy
    property color panelColor: "#111823"
    property color borderColor: "#303d51"
    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"

    signal addRequested
    signal removeRequested(int sourceIndex)
    signal referenceRequested(int sourceIndex)

    objectName: "activeSourceStrip"
    height: sourceCount > 0 ? 42 : 0
    visible: sourceCount > 0
    color: panelColor
    border.color: borderColor

    Row {
        id: chips

        spacing: 7
        anchors {
            fill: parent
            leftMargin: 12
            rightMargin: 12
            topMargin: 6
            bottomMargin: 6
        }

        Repeater {
            objectName: "activeSourceRepeater"
            model: control.sourcesModel

            delegate: Rectangle {
                id: chip

                required property int sourceId
                required property string filename

                height: chips.height
                width: Math.min(280, Math.max(128, chipText.implicitWidth + 62))
                radius: 15
                color: chip.sourceId === control.canonicalSourceIndex ? "#243f68" : "#1d2635"
                border.color: chip.sourceId === control.canonicalSourceIndex ? control.accentColor : control.borderColor

                Text {
                    id: chipText

                    text: qsTr("%1 · %2").arg(String.fromCharCode(65 + chip.sourceId)).arg(chip.filename)
                    color: control.textColor
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: roleButton.left
                        rightMargin: 6
                        verticalCenter: parent.verticalCenter
                    }
                }

                ToolButton {
                    id: roleButton

                    width: 30
                    height: 30
                    text: chip.sourceId === control.canonicalSourceIndex ? "R" : "⋯"
                    enabled: !control.busy
                    ToolTip.visible: hovered
                    ToolTip.text: chip.sourceId === control.canonicalSourceIndex ? qsTr("Canonical reference") : qsTr("Make Source %1 the reference").arg(String.fromCharCode(65 + chip.sourceId))
                    onClicked: sourceMenu.open()
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }

                    Menu {
                        id: sourceMenu

                        MenuItem {
                            text: qsTr("Use as reference")
                            enabled: chip.sourceId !== control.canonicalSourceIndex && !control.busy
                            onTriggered: control.referenceRequested(chip.sourceId)
                        }
                        MenuItem {
                            text: qsTr("Remove source")
                            enabled: control.sourceCount > 1 && !control.busy
                            onTriggered: control.removeRequested(chip.sourceId)
                        }
                    }
                }
            }
        }

        ToolButton {
            objectName: "addSourceChipButton"
            visible: control.sourceCount < 3
            width: 34
            height: chips.height
            text: "+"
            enabled: !control.busy
            Accessible.name: qsTr("Add source")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Add a source to this review")
            onClicked: control.addRequested()
        }
    }
}
