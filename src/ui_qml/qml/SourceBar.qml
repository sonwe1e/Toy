pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property var sourceNames
    required property var sourceSelected
    required property var sourceErrors
    required property int referenceSourceIndex
    required property int sourceCount
    required property bool graphicsReady
    required property bool busy
    required property bool canOpen
    property color panelColor: "#111823"
    property color cardColor: "#1d2635"
    property color borderColor: "#303d51"
    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"
    property color errorColor: "#f87171"

    signal browseRequested(int sourceIndex)
    signal removeRequested(int sourceIndex)
    signal openRequested

    color: panelColor
    border.color: borderColor

    Row {
        id: sourceRow

        spacing: 12
        anchors {
            fill: parent
            margins: 8
        }

        Repeater {
            model: 3

            delegate: Rectangle {
                id: sourceCard

                required property int index
                readonly property string letter: String.fromCharCode(65 + index)

                width: (sourceRow.width - openButton.width - sourceRow.spacing * 3) / 3
                height: sourceRow.height
                radius: 6
                color: control.cardColor
                border.color: control.sourceErrors[index] ? control.errorColor : control.borderColor

                Text {
                    id: sourceRole

                    text: control.referenceSourceIndex === sourceCard.index ? qsTr("REFERENCE") : (sourceCard.index === 2 ? qsTr("SOURCE C (OPTIONAL)") : qsTr("SOURCE %1").arg(sourceCard.letter))
                    color: "#9fc3ff"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    anchors {
                        top: parent.top
                        topMargin: 11
                        left: parent.left
                        leftMargin: 14
                    }
                }

                Text {
                    objectName: "source" + sourceCard.letter + "Filename"
                    text: control.sourceNames[sourceCard.index]
                    color: control.sourceSelected[sourceCard.index] ? control.textColor : control.mutedTextColor
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                    anchors {
                        top: sourceRole.bottom
                        topMargin: 5
                        left: parent.left
                        leftMargin: 14
                        right: removeButton.visible ? removeButton.left : browseButton.left
                        rightMargin: 8
                    }
                }

                ReviewActionButton {
                    id: browseButton

                    objectName: "selectSource" + sourceCard.letter + "Button"
                    implicitWidth: 78
                    text: sourceCard.index === 0 ? qsTr("Select A") : (control.sourceSelected[sourceCard.index] ? qsTr("Change") : qsTr("Add %1").arg(sourceCard.letter))
                    textColor: control.textColor
                    accentColor: control.accentColor
                    enabled: control.graphicsReady && !control.busy
                    Accessible.name: qsTr("Select source %1").arg(sourceCard.letter)
                    onClicked: control.browseRequested(sourceCard.index)
                    anchors {
                        right: parent.right
                        rightMargin: 10
                        verticalCenter: parent.verticalCenter
                    }
                }

                ReviewActionButton {
                    id: removeButton

                    objectName: "removeSource" + sourceCard.letter + "Button"
                    visible: sourceCard.index > 0 && control.sourceCount > sourceCard.index
                    implicitWidth: 34
                    text: "×"
                    textColor: control.textColor
                    accentColor: control.accentColor
                    helpText: qsTr("Remove Source %1 and rebuild the current review at the same media time.").arg(sourceCard.letter)
                    enabled: !control.busy
                    onClicked: control.removeRequested(sourceCard.index)
                    anchors {
                        right: browseButton.left
                        rightMargin: 6
                        verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        ReviewActionButton {
            id: openButton

            objectName: "openPairButton"
            width: 148
            height: 46
            text: qsTr("Open")
            textColor: control.textColor
            accentColor: control.accentColor
            prominent: true
            enabled: control.sourceSelected[0] && control.graphicsReady && !control.busy && control.canOpen
            Accessible.name: qsTr("Open selected review sources")
            onClicked: control.openRequested()
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
