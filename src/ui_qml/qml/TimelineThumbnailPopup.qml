pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: control

    required property int previewFrame
    required property string previewTimecode
    property string comparisonState: ""
    property url thumbnailSource: ""

    objectName: "timelineThumbnailPopup"
    width: 184
    height: 112
    radius: 7
    color: "#f0171e2a"
    border.color: "#50637f"

    Rectangle {
        width: parent.width - 12
        height: 66
        radius: 4
        color: "#090d14"
        anchors {
            top: parent.top
            topMargin: 6
            horizontalCenter: parent.horizontalCenter
        }

        Image {
            anchors.fill: parent
            source: control.thumbnailSource
            visible: source.toString().length > 0
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
        }

        Text {
            text: qsTr("Preview caches during playback")
            visible: control.thumbnailSource.toString().length === 0
            color: "#6f8099"
            font.pixelSize: 10
            anchors.centerIn: parent
        }
    }

    Text {
        text: qsTr("%1  ·  Frame %2").arg(control.previewTimecode).arg(control.previewFrame + 1)
        color: "#f3f6fb"
        font.pixelSize: 11
        anchors {
            left: parent.left
            leftMargin: 8
            bottom: comparisonLabel.top
            bottomMargin: 2
        }
    }

    Text {
        id: comparisonLabel

        text: control.comparisonState
        visible: text.length > 0
        color: "#9fc3ff"
        font.pixelSize: 10
        anchors {
            left: parent.left
            leftMargin: 8
            bottom: parent.bottom
            bottomMargin: 6
        }
    }
}
