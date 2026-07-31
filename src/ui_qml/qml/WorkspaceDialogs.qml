pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: control

    required property var host
    readonly property bool unsavedVisible: unsavedChangesDialog.visible

    signal saveRequested
    signal discardRequested
    signal cancelRequested

    function openUnsaved() {
        unsavedChangesDialog.open();
    }

    Dialog {
        id: unsavedChangesDialog

        objectName: "unsavedChangesDialog"
        parent: Overlay.overlay
        anchors.centerIn: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("Save changes to this review?")
        closePolicy: Popup.NoAutoClose
        width: Math.min(480, parent.width - 48)
        height: 190

        contentItem: Text {
            text: qsTr("The current review contains changes that have not been saved. Save them before continuing?")
            color: control.host.primaryTextColor
            wrapMode: Text.WordWrap
        }

        footer: DialogButtonBox {
            Button {
                objectName: "unsavedSaveButton"
                text: qsTr("Save")
                onClicked: {
                    unsavedChangesDialog.close();
                    control.saveRequested();
                }
            }
            Button {
                objectName: "unsavedDiscardButton"
                text: qsTr("Discard")
                onClicked: {
                    unsavedChangesDialog.close();
                    control.discardRequested();
                }
            }
            Button {
                objectName: "unsavedCancelButton"
                text: qsTr("Cancel")
                onClicked: {
                    unsavedChangesDialog.close();
                    control.cancelRequested();
                }
            }
        }
    }
}
