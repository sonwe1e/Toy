pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: control

    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"

    signal openVideosRequested
    signal openProjectRequested

    objectName: "emptyReviewView"
    color: "transparent"

    Column {
        spacing: 14
        anchors.centerIn: parent

        Text {
            text: qsTr("Drop one to three videos")
            color: control.textColor
            font.pixelSize: 24
            font.weight: Font.DemiBold
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: qsTr("Open one video to play, or two to three videos to compare frame by frame.")
            color: control.mutedTextColor
            font.pixelSize: 13
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Row {
            spacing: 10
            anchors.horizontalCenter: parent.horizontalCenter

            Button {
                id: openVideosButton

                objectName: "emptyOpenVideosButton"
                text: qsTr("Open videos")
                highlighted: true
                onClicked: control.openVideosRequested()

                implicitHeight: 40
                leftPadding: 17
                rightPadding: 17

                contentItem: Text {
                    text: openVideosButton.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: openVideosButton.enabled ? "#f3f6fb" : "#637086"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 5
                    color: !openVideosButton.enabled ? "#202938" : (openVideosButton.down ? "#2662bd" : (openVideosButton.hovered ? "#4f94ff" : "#4b8df8"))
                    border.width: openVideosButton.activeFocus ? 2 : 1
                    border.color: openVideosButton.activeFocus ? "#b7d3ff" : (openVideosButton.enabled ? "#72a7fa" : "#2a3444")
                }
            }

            Button {
                id: openProjectButton

                objectName: "emptyOpenProjectButton"
                text: qsTr("Open project")
                onClicked: control.openProjectRequested()

                implicitHeight: 40
                leftPadding: 17
                rightPadding: 17

                contentItem: Text {
                    text: openProjectButton.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: openProjectButton.enabled ? "#f3f6fb" : "#637086"
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 5
                    color: !openProjectButton.enabled ? "#202938" : (openProjectButton.down ? "#285da9" : (openProjectButton.hovered ? "#2d69bf" : "#253247"))
                    border.width: openProjectButton.activeFocus ? 2 : 1
                    border.color: openProjectButton.activeFocus ? "#4b8df8" : (openProjectButton.enabled ? "#3b4d67" : "#2a3444")
                }
            }
        }
    }
}
