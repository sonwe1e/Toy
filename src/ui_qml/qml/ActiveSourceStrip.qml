pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "VcsTheme.js" as Theme

Rectangle {
    id: control

    required property var sourcesModel
    required property int sourceCount
    required property bool singleMode
    required property int canonicalSourceIndex
    required property string canonicalSourceIdentity
    required property var pendingSourceIdentities
    required property var sourceIdentities
    property color panelColor: Theme.panel
    property color borderColor: Theme.border
    property color accentColor: Theme.accent
    property color textColor: Theme.primaryText
    property color mutedTextColor: Theme.mutedText

    signal addRequested
    signal removeRequested(string sourceIdentity)
    signal referenceRequested(string sourceIdentity)
    signal viewerFocusRequested

    property int openMenuCount: 0
    property bool returnViewerFocusAfterClose: false
    readonly property bool anyMenuOpen: openMenuCount > 0

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
                required property string sourceIdentity
                required property string filename
                required property bool changedOnDisk

                height: chips.height
                width: Math.min(280, Math.max(128, chipText.implicitWidth + (control.singleMode ? 24 : 84)))
                radius: 15
                readonly property string resolvedSourceIdentity: chip.sourceIdentity.length > 0 ? chip.sourceIdentity : (chip.sourceId >= 0 && chip.sourceId < control.sourceIdentities.length ? String(control.sourceIdentities[chip.sourceId]) : "")
                readonly property bool isCanonical: chip.resolvedSourceIdentity.length > 0 ? chip.resolvedSourceIdentity === control.canonicalSourceIdentity : chip.sourceId === control.canonicalSourceIndex
                readonly property bool pending: control.pendingSourceIdentities.indexOf(chip.resolvedSourceIdentity) >= 0 || requestQueued
                property bool requestQueued: false
                color: chip.isCanonical ? Theme.controlChecked : Theme.raisedPanel
                border.color: chip.isCanonical ? control.accentColor : control.borderColor

                function requestReference() {
                    if (chip.pending || chip.isCanonical || chip.resolvedSourceIdentity.length === 0)
                        return;
                    chip.requestQueued = true;
                    control.returnViewerFocusAfterClose = true;
                    sourceMenu.close();
                    Qt.callLater(() => {
                        control.referenceRequested(chip.resolvedSourceIdentity);
                        chip.requestQueued = false;
                    });
                }

                function requestRemoval() {
                    if (chip.pending || chip.resolvedSourceIdentity.length === 0)
                        return;
                    chip.requestQueued = true;
                    control.returnViewerFocusAfterClose = true;
                    sourceMenu.close();
                    Qt.callLater(() => {
                        control.removeRequested(chip.resolvedSourceIdentity);
                        chip.requestQueued = false;
                    });
                }

                Text {
                    id: chipText

                    text: qsTr("%1 · %2").arg(String.fromCharCode(65 + chip.sourceId)).arg(chip.filename)
                    color: control.textColor
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    anchors {
                        left: parent.left
                        leftMargin: 12
                        right: control.singleMode ? parent.right : chipControls.left
                        rightMargin: control.singleMode ? 12 : 6
                        verticalCenter: parent.verticalCenter
                    }
                }

                Row {
                    id: chipControls

                    visible: !control.singleMode
                    spacing: 3
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }

                    BusyIndicator {
                        id: pendingIndicator

                        visible: chip.pending
                        running: visible
                        width: 22
                        height: 22
                    }

                    Rectangle {
                        id: referenceBadge

                        objectName: "sourceReferenceBadge-" + chip.sourceId
                        visible: chip.isCanonical
                        width: 24
                        height: 24
                        radius: 6
                        color: Theme.control
                        border.width: 1
                        border.color: Theme.controlBorder
                        Accessible.name: qsTr("Canonical reference")

                        Text {
                            anchors.centerIn: parent
                            text: "R"
                            color: control.textColor
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Rectangle {
                        id: changedOnDiskBadge

                        objectName: "changedOnDiskBadge-" + chip.sourceId
                        visible: chip.changedOnDisk
                        width: 24
                        height: 24
                        radius: 6
                        color: "#3d2e10"
                        border.width: 1
                        border.color: "#b08630"
                        Accessible.name: qsTr("Video changed on disk")
                        VcsToolTip {
                            visible: changedOnDiskHover.hovered
                            text: qsTr("Video changed on disk")
                        }

                        HoverHandler {
                            id: changedOnDiskHover
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "!"
                            color: "#e6a817"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    VcsToolButton {
                        id: overflowButton

                        objectName: "sourceOverflowButton-" + chip.sourceId
                        readonly property var sourceMenuControl: sourceMenu
                        readonly property var makeReferenceAction: useAsReferenceItem
                        readonly property var removeSourceAction: removeSourceItem
                        width: 30
                        height: 30
                        text: "⋯"
                        enabled: !chip.pending && chip.resolvedSourceIdentity.length > 0
                        helpText: chip.pending ? qsTr("Updating video…") : qsTr("Source actions")
                        Accessible.name: qsTr("Source actions for %1").arg(chip.filename)
                        onClicked: sourceMenu.popup(overflowButton, Qt.point(0, overflowButton.height))
                    }

                    VcsMenu {
                        id: sourceMenu

                        objectName: "sourceMenu-" + chip.sourceId
                        property bool wasOpened: false
                        onOpened: {
                            wasOpened = true;
                            control.openMenuCount += 1;
                        }
                        onClosed: {
                            if (wasOpened) {
                                wasOpened = false;
                                control.openMenuCount = Math.max(0, control.openMenuCount - 1);
                            }
                            if (control.returnViewerFocusAfterClose) {
                                control.returnViewerFocusAfterClose = false;
                                control.viewerFocusRequested();
                            }
                        }
                        Component.onDestruction: {
                            if (wasOpened) {
                                wasOpened = false;
                                control.openMenuCount = Math.max(0, control.openMenuCount - 1);
                            }
                            if (control.returnViewerFocusAfterClose) {
                                control.returnViewerFocusAfterClose = false;
                                control.viewerFocusRequested();
                            }
                        }

                        VcsMenuItem {
                            id: useAsReferenceItem

                            objectName: "makeReferenceAction-" + chip.sourceId
                            visible: !chip.isCanonical
                            text: qsTr("Make reference")
                            enabled: !chip.pending
                            onTriggered: chip.requestReference()
                        }

                        VcsMenuItem {
                            id: removeSourceItem

                            objectName: "removeSourceAction-" + chip.sourceId
                            text: qsTr("Remove video")
                            enabled: control.sourceCount > 1
                            onTriggered: chip.requestRemoval()
                        }
                    }
                }
            }
        }

        VcsToolButton {
            id: addSourceChipButton

            objectName: "addSourceChipButton"
            visible: control.sourceCount < 3
            width: 34
            height: chips.height
            text: "+"
            enabled: true
            helpText: qsTr("Add a video")
            onClicked: control.addRequested()
            controlRadius: 15
        }
    }
}
