pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

Dialog {
    id: control

    required property var pendingVideos
    required property var fileNameFunction
    property int initialReferenceIndex: 0
    readonly property int referenceIndex: referenceCombo.currentIndex

    signal moveRequested(int fromIndex, int toIndex)

    component CompactMoveButton: VcsToolButton {
        id: button

        implicitWidth: 52
        implicitHeight: 30
        labelPixelSize: 14
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
        color: Theme.modalScrim
    }

    background: Rectangle {
        objectName: "dropDialogBackground"
        color: Theme.menu
        radius: 10
        border.width: 1
        border.color: Theme.menuBorder
    }

    header: Rectangle {
        objectName: "dropDialogHeader"
        implicitHeight: 52
        color: Theme.panel
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
            color: Theme.primaryText
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
            color: Theme.mutedText
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
                    color: Theme.primaryText
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
                color: Theme.primaryText
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

        delegate: ReviewActionButton {
            id: footerButton

            objectName: "dropFooterStandardButton"
            implicitWidth: 88
            implicitHeight: 30
        }

        background: Rectangle {
            color: Theme.panel
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
