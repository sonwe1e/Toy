pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Menu {
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

    Menu {
        objectName: "contextViewMenu"
        title: qsTr("View")
        enabled: control.sourceCount > 1

        MenuItem {
            objectName: "contextSideAction"
            text: qsTr("Side by side")
            onTriggered: control.sideRequested()
        }
        MenuItem {
            objectName: "contextWipeAction"
            text: qsTr("Wipe")
            onTriggered: control.wipeRequested()
        }
        MenuItem {
            objectName: "contextDifferenceAction"
            text: qsTr("Difference")
            onTriggered: control.diffRequested()
        }
    }
    MenuSeparator {
        visible: control.sourceCount > 1
    }
    Menu {
        objectName: "contextPairMenu"
        title: qsTr("Pair")
        enabled: control.sourceCount === 3
        visible: control.sourceCount > 1

        Repeater {
            model: control.differenceEdges

            delegate: MenuItem {
                required property int index
                required property var modelData
                text: String(modelData.label)
                checkable: true
                checked: index === control.currentEdgeIndex
                onTriggered: control.edgeRequested(Number(modelData.preferenceValue))
            }
        }
    }
    Menu {
        objectName: "contextReferenceMenu"
        title: qsTr("Reference")
        enabled: control.sourceCount > 1

        MenuItem {
            text: qsTr("Source A")
            checkable: true
            checked: control.canonicalSourceIndex === 0
            onTriggered: control.referenceRequested(0)
        }
        MenuItem {
            text: qsTr("Source B")
            visible: control.sourceCount > 1
            checkable: true
            checked: control.canonicalSourceIndex === 1
            onTriggered: control.referenceRequested(1)
        }
        MenuItem {
            text: qsTr("Source C")
            visible: control.sourceCount > 2
            checkable: true
            checked: control.canonicalSourceIndex === 2
            onTriggered: control.referenceRequested(2)
        }
    }
    MenuSeparator {}
    MenuItem {
        objectName: "contextBadCaseAction"
        text: qsTr("Export Bad Case…")
        enabled: control.canExport
        onTriggered: control.badCaseRequested()
    }
    MenuItem {
        objectName: "contextInfoAction"
        text: qsTr("Inspector and media info")
        onTriggered: control.inspectorRequested()
    }
    MenuItem {
        text: qsTr("Full screen")
        onTriggered: control.fullScreenRequested()
    }
}
