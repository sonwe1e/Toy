pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Dialog {
    id: control

    required property var pendingVideos
    required property var fileNameFunction
    readonly property int referenceIndex: referenceCombo.currentIndex === pendingVideos.length ? -1 : referenceCombo.currentIndex

    signal moveRequested(int fromIndex, int toIndex)

    objectName: "dropReviewDialog"
    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    popupType: Popup.Item
    modal: true
    dim: true
    focus: true
    padding: 20
    width: Math.min(600, parent.width - 48)
    title: qsTr("Confirm source order and Reference")
    closePolicy: Popup.CloseOnEscape
    onOpened: referenceCombo.currentIndex = 0

    Overlay.modal: Rectangle {
        color: "#99060a10"
    }

    background: Rectangle {
        objectName: "dropDialogBackground"
        color: "#ff171e2a"
        radius: 10
        border.width: 1
        border.color: "#40516a"
    }

    header: Rectangle {
        objectName: "dropDialogHeader"
        implicitHeight: 52
        color: "#ff111823"
        radius: 10

        Rectangle {
            height: 10
            color: parent.color
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
        }

        Text {
            text: control.title
            color: "#fff3f6fb"
            font.pixelSize: 16
            font.weight: Font.DemiBold
            anchors {
                left: parent.left
                leftMargin: 20
                verticalCenter: parent.verticalCenter
            }
        }
    }

    contentItem: Column {
        spacing: 12

        Text {
            width: parent.width
            text: qsTr("The order below becomes Source A/B/C. Choose which stream owns the canonical frame index before opening.")
            color: "#ff93a2ba"
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: control.pendingVideos

            delegate: Row {
                id: droppedRow

                required property int index
                required property var modelData

                width: parent.width
                spacing: 8

                Text {
                    width: 34
                    text: String.fromCharCode(65 + droppedRow.index)
                    color: "#ff9fc3ff"
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - 162
                    text: control.fileNameFunction(droppedRow.modelData)
                    color: "#fff3f6fb"
                    elide: Text.ElideMiddle
                    anchors.verticalCenter: parent.verticalCenter
                }
                Button {
                    implicitWidth: 52
                    implicitHeight: 30
                    text: "↑"
                    enabled: droppedRow.index > 0
                    Accessible.name: qsTr("Move source earlier")
                    onClicked: control.moveRequested(droppedRow.index, droppedRow.index - 1)
                }
                Button {
                    implicitWidth: 52
                    implicitHeight: 30
                    text: "↓"
                    enabled: droppedRow.index + 1 < control.pendingVideos.length
                    Accessible.name: qsTr("Move source later")
                    onClicked: control.moveRequested(droppedRow.index, droppedRow.index + 1)
                }
            }
        }

        Row {
            spacing: 12

            Text {
                text: qsTr("Reference")
                color: "#fff3f6fb"
                anchors.verticalCenter: parent.verticalCenter
            }
            ComboBox {
                id: referenceCombo

                objectName: "dropReferenceCombo"
                width: 240
                model: control.pendingVideos.length === 3 ? [qsTr("Source A"), qsTr("Source B"), qsTr("Source C"), qsTr("None (prediction-only)")] : [qsTr("Source A"), qsTr("Source B"), qsTr("None (prediction-only)")]
            }
        }
    }

    footer: DialogButtonBox {
        objectName: "dropDialogFooter"
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        padding: 12

        background: Rectangle {
            color: "#ff111823"
            radius: 10

            Rectangle {
                height: 10
                color: parent.color
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                }
            }
        }
    }
}
