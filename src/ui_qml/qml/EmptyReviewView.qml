pragma ComponentBehavior: Bound

import QtQuick
import "VcsTheme.js" as Theme

Rectangle {
    id: control

    property color accentColor: Theme.accent
    property color textColor: Theme.primaryText
    property color mutedTextColor: Theme.mutedText

    signal openVideosRequested

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

        Text {
            text: qsTr("Visual playback only · Audio is not played")
            color: control.mutedTextColor
            font.pixelSize: 12
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Row {
            spacing: 10
            anchors.horizontalCenter: parent.horizontalCenter

            ReviewActionButton {
                id: openVideosButton

                objectName: "emptyOpenVideosButton"
                text: qsTr("Open videos")
                prominent: true
                onClicked: control.openVideosRequested()
            }
        }
    }
}
