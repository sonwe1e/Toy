pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Dialog {
    id: control

    required property var pendingVideos
    required property var fileNameFunction
    property int initialReferenceIndex: 0
    readonly property int referenceIndex: referenceCombo.currentIndex

    signal moveRequested(int fromIndex, int toIndex)

    component CompactMoveButton: Button {
        id: button

        implicitWidth: 52
        implicitHeight: 30
        padding: 0

        contentItem: Text {
            text: button.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: button.enabled ? "#f3f6fb" : "#637086"
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        background: Rectangle {
            radius: 5
            color: !button.enabled ? "#202938" : (button.down ? "#285da9" : (button.hovered ? "#2d69bf" : "#253247"))
            border.width: button.activeFocus ? 2 : 1
            border.color: button.activeFocus ? "#4b8df8" : "#3b4d67"
        }
    }

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
    onOpened: referenceCombo.currentIndex = Math.max(0, Math.min(initialReferenceIndex, pendingVideos.length - 1))

    function requestMove(fromIndex, toIndex) {
        const selected = referenceCombo.currentIndex;
        if (selected === fromIndex)
            referenceCombo.currentIndex = toIndex;
        else if (fromIndex < toIndex && selected > fromIndex && selected <= toIndex)
            referenceCombo.currentIndex = selected - 1;
        else if (toIndex < fromIndex && selected >= toIndex && selected < fromIndex)
            referenceCombo.currentIndex = selected + 1;
        moveRequested(fromIndex, toIndex);
    }

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
            width: parent.width

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
                CompactMoveButton {
                    text: "↑"
                    enabled: droppedRow.index > 0
                    Accessible.name: qsTr("Move source earlier")
                    onClicked: control.requestMove(droppedRow.index, droppedRow.index - 1)
                }
                CompactMoveButton {
                    text: "↓"
                    enabled: droppedRow.index + 1 < control.pendingVideos.length
                    Accessible.name: qsTr("Move source later")
                    onClicked: control.requestMove(droppedRow.index, droppedRow.index + 1)
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
            ToolbarCombo {
                id: referenceCombo

                objectName: "dropReferenceCombo"
                width: 240
                model: control.pendingVideos.length === 3 ? [qsTr("Source A"), qsTr("Source B"), qsTr("Source C")] : [qsTr("Source A"), qsTr("Source B")]
            }
        }
    }

    footer: DialogButtonBox {
        objectName: "dropDialogFooter"
        standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
        spacing: 8
        alignment: Qt.AlignRight
        padding: 12

        delegate: Button {
            id: footerButton

            objectName: "dropFooterStandardButton"
            implicitWidth: 88
            implicitHeight: 30
            padding: 0

            contentItem: Text {
                text: footerButton.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: footerButton.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            background: Rectangle {
                radius: 5
                color: !footerButton.enabled ? "#202938" : (footerButton.down ? "#285da9" : (footerButton.hovered ? "#2d69bf" : "#253247"))
                border.width: footerButton.activeFocus ? 2 : 1
                border.color: footerButton.activeFocus ? "#4b8df8" : "#3b4d67"
            }
        }

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
