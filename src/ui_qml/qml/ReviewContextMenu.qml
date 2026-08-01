pragma ComponentBehavior: Bound

import QtQuick

VcsMenu {
    id: control

    required property int sourceCount
    required property int canonicalSourceIndex
    required property bool canExport
    required property var differenceEdges
    required property int currentEdgeIndex

    signal sideRequested
    signal wipeRequested
    signal diffRequested
    signal edgeRequested(int preferenceValue)
    signal referenceRequested(int sourceIndex)
    signal badCaseRequested
    signal inspectorRequested
    signal fullScreenRequested

    objectName: "reviewContextMenu"

    VcsMenu {
        objectName: "contextViewMenu"
        title: qsTr("View")
        enabled: control.sourceCount > 1

        VcsMenuItem {
            objectName: "contextSideAction"
            text: qsTr("Side by side")
            onTriggered: control.sideRequested()
        }
        VcsMenuItem {
            objectName: "contextWipeAction"
            text: qsTr("Wipe")
            onTriggered: control.wipeRequested()
        }
        VcsMenuItem {
            objectName: "contextDifferenceAction"
            text: qsTr("Difference")
            onTriggered: control.diffRequested()
        }
    }
    VcsMenuSeparator {
        visible: control.sourceCount > 1
    }
    VcsMenu {
        objectName: "contextPairMenu"
        title: qsTr("Pair")
        enabled: control.sourceCount === 3
        visible: control.sourceCount > 1

        Repeater {
            model: control.differenceEdges

            delegate: VcsMenuItem {
                required property int index
                required property var modelData
                text: String(modelData.label)
                checkable: true
                checked: index === control.currentEdgeIndex
                onTriggered: control.edgeRequested(Number(modelData.preferenceValue))
            }
        }
    }
    VcsMenu {
        objectName: "contextReferenceMenu"
        title: qsTr("Reference")
        enabled: control.sourceCount > 1

        VcsMenuItem {
            text: qsTr("Source A")
            checkable: true
            checked: control.canonicalSourceIndex === 0
            onTriggered: control.referenceRequested(0)
        }
        VcsMenuItem {
            text: qsTr("Source B")
            visible: control.sourceCount > 1
            checkable: true
            checked: control.canonicalSourceIndex === 1
            onTriggered: control.referenceRequested(1)
        }
        VcsMenuItem {
            text: qsTr("Source C")
            visible: control.sourceCount > 2
            checkable: true
            checked: control.canonicalSourceIndex === 2
            onTriggered: control.referenceRequested(2)
        }
    }
    VcsMenuSeparator {}
    VcsMenuItem {
        objectName: "contextBadCaseAction"
        text: qsTr("Export Bad Case…")
        enabled: control.canExport
        onTriggered: control.badCaseRequested()
    }
    VcsMenuItem {
        objectName: "contextInfoAction"
        text: qsTr("Inspector and media info")
        onTriggered: control.inspectorRequested()
    }
    VcsMenuItem {
        text: qsTr("Full screen")
        onTriggered: control.fullScreenRequested()
    }
}
