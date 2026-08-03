pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Rectangle {
    id: control

    required property var sourcesModel
    required property int sourceCount
    required property bool singleMode
    required property int canonicalSourceIndex
    required property string canonicalSourceIdentity
    required property var pendingSourceIdentities
    required property var sourceIdentities
    property color panelColor: "#111823"
    property color borderColor: "#303d51"
    property color accentColor: "#4b8df8"
    property color textColor: "#f3f6fb"
    property color mutedTextColor: "#93a2ba"

    signal addRequested
    signal removeRequested(string sourceIdentity)
    signal referenceRequested(string sourceIdentity)
    signal viewerFocusRequested

    property int openMenuCount: 0
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

                height: chips.height
                width: Math.min(280, Math.max(128, chipText.implicitWidth + (control.singleMode ? 24 : 84)))
                radius: 15
                readonly property string resolvedSourceIdentity: chip.sourceIdentity.length > 0 ? chip.sourceIdentity : (chip.sourceId >= 0 && chip.sourceId < control.sourceIdentities.length ? String(control.sourceIdentities[chip.sourceId]) : "")
                readonly property bool isCanonical: chip.resolvedSourceIdentity.length > 0 ? chip.resolvedSourceIdentity === control.canonicalSourceIdentity : chip.sourceId === control.canonicalSourceIndex
                readonly property bool pending: control.pendingSourceIdentities.indexOf(chip.resolvedSourceIdentity) >= 0 || requestQueued
                property bool requestQueued: false
                color: chip.isCanonical ? "#243f68" : "#1d2635"
                border.color: chip.isCanonical ? control.accentColor : control.borderColor

                function requestReference() {
                    if (chip.pending || chip.isCanonical || chip.resolvedSourceIdentity.length === 0)
                        return;
                    chip.requestQueued = true;
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
                        color: "#253247"
                        border.width: 1
                        border.color: "#3b4d67"
                        Accessible.name: qsTr("Canonical reference")

                        Text {
                            anchors.centerIn: parent
                            text: "R"
                            color: control.textColor
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    ToolButton {
                        id: overflowButton

                        objectName: "sourceOverflowButton-" + chip.sourceId
                        readonly property var sourceMenuControl: sourceMenu
                        readonly property var makeReferenceAction: useAsReferenceItem
                        readonly property var removeSourceAction: removeSourceItem
                        width: 30
                        height: 30
                        text: "⋯"
                        enabled: !chip.pending && chip.resolvedSourceIdentity.length > 0
                        padding: 0
                        Accessible.name: qsTr("Source actions for %1").arg(chip.filename)
                        ToolTip.visible: hovered
                        ToolTip.text: chip.pending ? qsTr("Updating video…") : qsTr("Source actions")
                        onClicked: sourceMenu.popup(overflowButton, Qt.point(0, overflowButton.height))

                        contentItem: Text {
                            text: overflowButton.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: overflowButton.enabled ? "#f3f6fb" : "#637086"
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        background: Rectangle {
                            radius: 6
                            color: !overflowButton.enabled ? "#202938" : (overflowButton.down ? "#285da9" : (overflowButton.hovered ? "#2d69bf" : "#253247"))
                            border.width: overflowButton.activeFocus ? 2 : 1
                            border.color: overflowButton.activeFocus ? "#4b8df8" : "#3b4d67"
                        }
                    }

                    VcsMenu {
                        id: sourceMenu

                        objectName: "sourceMenu-" + chip.sourceId
                        onOpened: control.openMenuCount += 1
                        onClosed: {
                            control.openMenuCount = Math.max(0, control.openMenuCount - 1);
                            control.viewerFocusRequested();
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

        ToolButton {
            id: addSourceChipButton

            objectName: "addSourceChipButton"
            visible: control.sourceCount < 3
            width: 34
            height: chips.height
            text: "+"
            enabled: true
            padding: 0
            Accessible.name: qsTr("Add video")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Add a video")
            onClicked: control.addRequested()

            contentItem: Text {
                text: addSourceChipButton.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: addSourceChipButton.enabled ? "#f3f6fb" : "#637086"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            background: Rectangle {
                radius: 15
                color: !addSourceChipButton.enabled ? "#202938" : (addSourceChipButton.down ? "#285da9" : (addSourceChipButton.hovered ? "#2d69bf" : "#253247"))
                border.width: addSourceChipButton.activeFocus ? 2 : 1
                border.color: addSourceChipButton.activeFocus ? "#4b8df8" : "#3b4d67"
            }
        }
    }
}
