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
                objectName: "emptyOpenVideosButton"
                text: qsTr("Open videos")
                highlighted: true
                onClicked: control.openVideosRequested()
            }

            Button {
                objectName: "emptyOpenProjectButton"
                text: qsTr("Open project")
                onClicked: control.openProjectRequested()
            }
        }
    }
}
