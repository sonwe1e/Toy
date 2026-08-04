pragma ComponentBehavior: Bound

import QtQuick

VcsMenu {
    id: control

    required property int sourceCount
    required property int canonicalSourceIndex
    required property int currentViewMode
    required property bool fullScreen
    required property var differenceEdges
    required property int currentEdgeIndex
    required property var sourceIdentities
    readonly property bool emptyStateOnly: sourceCount === 0
    readonly property int availableActionCount: {
        if (emptyStateOnly || sourceCount === 1)
            return 2;
        return sourceCount === 3 ? 5 : 4;
    }

    signal sideRequested
    signal wipeRequested
    signal diffRequested
    signal edgeRequested(int preferenceValue)
    signal referenceRequested(string sourceIdentity)
    signal openRequested
    signal inspectorRequested
    signal fullScreenRequested
    signal viewerFocusRequested

    property bool returnViewerFocusAfterClose: false
    objectName: "reviewContextMenu"
    readonly property bool anyMenuOpen: control.opened

    function changeReferenceByIndex(sourceIndex) {
        if (sourceIndex < 0 || sourceIndex >= control.sourceIdentities.length)
            return false;
        return control.referenceRequested(String(control.sourceIdentities[sourceIndex]));
    }

    onClosed: {
        if (control.returnViewerFocusAfterClose) {
            control.returnViewerFocusAfterClose = false;
            control.viewerFocusRequested();
        }
    }

    VcsMenuItem {
        objectName: "contextOpenAction"
        text: qsTr("Open videos…")
        visible: control.sourceCount === 0
        onTriggered: control.openRequested()
    }

    VcsMenu {
        objectName: "contextViewMenu"
        title: qsTr("View")
        enabled: control.sourceCount > 1
        visible: control.sourceCount > 1

        VcsRadioMenuItem {
            objectName: "contextSideAction"
            text: qsTr("Side by side")
            checked: control.currentViewMode === 0
            onTriggered: {
                control.sideRequested();
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            objectName: "contextWipeAction"
            text: qsTr("Wipe")
            checked: control.currentViewMode === 5
            onTriggered: {
                control.wipeRequested();
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            objectName: "contextDifferenceAction"
            text: qsTr("Difference")
            checked: control.currentViewMode === 3
            onTriggered: {
                control.diffRequested();
                control.returnViewerFocusAfterClose = true;
            }
        }
    }
    VcsMenuSeparator {
        visible: control.sourceCount > 1
    }
    VcsMenu {
        objectName: "contextPairMenu"
        title: qsTr("Pair")
        enabled: control.sourceCount === 3
        visible: control.sourceCount === 3

        Repeater {
            model: control.differenceEdges

            delegate: VcsRadioMenuItem {
                required property int index
                required property var modelData
                text: String(modelData.label)
                checked: index === control.currentEdgeIndex
                onTriggered: {
                    control.edgeRequested(Number(modelData.preferenceValue));
                    control.returnViewerFocusAfterClose = true;
                }
            }
        }
    }
    VcsMenu {
        objectName: "contextReferenceMenu"
        title: qsTr("Reference")
        enabled: control.sourceCount > 1
        visible: control.sourceCount > 1

        VcsRadioMenuItem {
            text: qsTr("Source A")
            checked: control.canonicalSourceIndex === 0
            onTriggered: {
                control.changeReferenceByIndex(0);
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            text: qsTr("Source B")
            visible: control.sourceCount > 1
            checked: control.canonicalSourceIndex === 1
            onTriggered: {
                control.changeReferenceByIndex(1);
                control.returnViewerFocusAfterClose = true;
            }
        }
        VcsRadioMenuItem {
            text: qsTr("Source C")
            visible: control.sourceCount > 2
            checked: control.canonicalSourceIndex === 2
            onTriggered: {
                control.changeReferenceByIndex(2);
                control.returnViewerFocusAfterClose = true;
            }
        }
    }
    VcsMenuSeparator {
        visible: control.sourceCount > 0
    }
    VcsMenuItem {
        objectName: "contextInfoAction"
        text: qsTr("Inspector and media info")
        visible: control.sourceCount > 0
        onTriggered: {
            control.inspectorRequested();
            control.returnViewerFocusAfterClose = true;
        }
    }
    VcsMenuItem {
        text: control.fullScreen ? qsTr("Exit full screen") : qsTr("Full screen")
        onTriggered: {
            control.fullScreenRequested();
            control.returnViewerFocusAfterClose = true;
        }
    }
}
