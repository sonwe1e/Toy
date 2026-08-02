pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: control

    required property var sourcesModel
    required property int sourceCount
    required property bool singleMode
    required property int canonicalSourceIndex
    required property bool busy
    required property var pendingSourceIndexes
    property color panelColor: "#111823"
    property color borderColor: "#303d51"
    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"

    signal addRequested
    signal removeRequested(int sourceIndex)
    signal referenceRequested(int sourceIndex)

    objectName: "activeSourceStrip"
    height: sourceCount > 0 ? (singleMode ? 38 : 42) : 0
    visible: sourceCount > 0
    color: singleMode ? "transparent" : panelColor
    border.color: singleMode ? "transparent" : borderColor
    opacity: singleMode && !sourceHover.hovered ? 0.68 : 1.0

    Behavior on opacity {
        NumberAnimation {
            duration: 160
        }
    }

    HoverHandler {
        id: sourceHover
    }

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
                width: Math.min(280, Math.max(128, chipText.implicitWidth + (control.singleMode ? 24 : 62)))
                radius: 15
                color: chip.sourceId === control.canonicalSourceIndex ? "#243f68" : "#1d2635"
                border.color: chip.sourceId === control.canonicalSourceIndex ? control.accentColor : control.borderColor
                readonly property bool pending: control.pendingSourceIndexes.indexOf(chip.sourceId) >= 0

                Text {
                    id: chipText

                    text: qsTr("%1 · %2").arg(String.fromCharCode(65 + chip.sourceId)).arg(chip.filename)
                    color: control.textColor
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: control.singleMode ? parent.right : pendingIndicator.left
                        rightMargin: control.singleMode ? 12 : 6
                        verticalCenter: parent.verticalCenter
                    }
                }

                BusyIndicator {
                    id: pendingIndicator

                    visible: chip.pending
                    running: visible
                    width: 22
                    height: 22
                    anchors {
                        right: roleButton.visible ? roleButton.left : parent.right
                        rightMargin: roleButton.visible ? 2 : 7
                        verticalCenter: parent.verticalCenter
                    }
                }

                ToolButton {
                    id: roleButton

                    visible: !control.singleMode
                    width: 30
                    height: 30
                    text: chip.sourceId === control.canonicalSourceIndex ? "R" : "⋯"
                    enabled: !chip.pending
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: chip.sourceId === control.canonicalSourceIndex ? qsTr("Canonical reference") : qsTr("Make Source %1 the reference").arg(String.fromCharCode(65 + chip.sourceId))
                    onClicked: {
                        sourceMenu.open();
                    }
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }

                    contentItem: Text {
                        text: roleButton.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: roleButton.enabled ? "#f3f6fb" : "#637086"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }

                    background: Rectangle {
                        radius: 6
                        color: !roleButton.enabled ? "#202938" : (roleButton.down ? "#285da9" : (roleButton.hovered ? "#2d69bf" : "#253247"))
                        border.width: roleButton.activeFocus ? 2 : 1
                        border.color: roleButton.activeFocus ? "#4b8df8" : "#3b4d67"
                    }

                    Menu {
                        id: sourceMenu

                        topMargin: 6
                        bottomMargin: 6
                        padding: 4

                        background: Rectangle {
                            radius: 5
                            color: "#1d2635"
                            border.color: "#303d51"
                            border.width: 1
                        }

                        MenuItem {
                            id: useAsReferenceItem

                            text: qsTr("Use as reference")
                            enabled: chip.sourceId !== control.canonicalSourceIndex
                            onTriggered: control.referenceRequested(chip.sourceId)
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 6
                            bottomPadding: 6

                            contentItem: Text {
                                text: useAsReferenceItem.text
                                color: useAsReferenceItem.enabled ? "#f3f6fb" : "#637086"
                                font.pixelSize: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                radius: 3
                                color: useAsReferenceItem.hovered || useAsReferenceItem.highlighted ? "#285da9" : "transparent"
                            }
                        }
                        MenuItem {
                            id: removeSourceItem

                            text: qsTr("Remove source")
                            enabled: control.sourceCount > 1
                            onTriggered: control.removeRequested(chip.sourceId)
                            leftPadding: 12
                            rightPadding: 12
                            topPadding: 6
                            bottomPadding: 6

                            contentItem: Text {
                                text: removeSourceItem.text
                                color: removeSourceItem.enabled ? "#f3f6fb" : "#637086"
                                font.pixelSize: 12
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                radius: 3
                                color: removeSourceItem.hovered || removeSourceItem.highlighted ? "#285da9" : "transparent"
                            }
                        }
                    }
                }
            }
        }

        ToolButton {
            id: addSourceChipButton

            objectName: "addSourceChipButton"
            visible: control.sourceCount < 3
            width: 34
            height: chips.height
            text: "+"
            enabled: true
            padding: 0
            Accessible.name: qsTr("Add video")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Add a video")
            onClicked: control.addRequested()

            contentItem: Text {
                text: addSourceChipButton.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: addSourceChipButton.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            background: Rectangle {
                radius: 15
                color: !addSourceChipButton.enabled ? "#202938" : (addSourceChipButton.down ? "#285da9" : (addSourceChipButton.hovered ? "#2d69bf" : "#253247"))
                border.width: addSourceChipButton.activeFocus ? 2 : 1
                border.color: addSourceChipButton.activeFocus ? "#4b8df8" : "#3b4d67"
            }
        }
    }
}
